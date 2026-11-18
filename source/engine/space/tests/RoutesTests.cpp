#include "Systems.h"

#include <jettison/ai/Courses.h>
#include <jettison/celestial/Frames.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Routes.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using Ai::Sphere;
using Space::Place;

namespace
{
Celestial::Galaxy Triangle(double ab, double bc, double ac)
{
	Celestial::Galaxy galaxy;
	galaxy.name = "Triangle";
	galaxy.systems = {
	    {"A", "systems/a.yaml", std::nullopt},
	    {"B", "systems/b.yaml", std::nullopt},
	    {"C", "systems/c.yaml", std::nullopt},
	    {"Lonely", "systems/lonely.yaml", std::nullopt},
	};
	galaxy.lanes = {
	    AiTests::Lane("A", "A to B", "B", "B to A", ab),
	    AiTests::Lane("B", "B to C", "C", "C to B", bc),
	    AiTests::Lane("A", "A to C", "C", "C to A", ac),
	};
	Celestial::Resolve(galaxy);
	return galaxy;
}


// Every leg of the course clear of the obstacles, the legs from its ends by their radii alone.
bool Clear(const Ai::Course &course, const std::vector<Sphere> &obstacles, double clearance)
{
	for (std::size_t leg = 1; leg < course.points.size(); ++leg)
	{
		const bool fromAnEnd = leg == 1 || leg + 1 == course.points.size();
		if (!Ai::LegClear(
		        course.points[leg - 1], course.points[leg], obstacles, fromAnEnd ? 0.0 : clearance))
			return false;
	}
	return true;
}


double Length(const Ai::Course &course)
{
	double length = 0.0;
	for (std::size_t leg = 1; leg < course.points.size(); ++leg)
		length += glm::length(course.points[leg] - course.points[leg - 1]);
	return length;
}
} // namespace


TEST_CASE("Routes: across the sample galaxy, end to end and back, by its lanes")
{
	Celestial::Galaxy galaxy = AiTests::Sample();
	galaxy.lanes[1].toll = 6000;

	const auto out = Space::PlotRoute(galaxy, {0, ""}, {2, ""});
	REQUIRE(out);
	CHECK(out->seconds == 105.0);
	REQUIRE(out->jumps.size() == 2);
	CHECK(out->jumps[0].lane == 0);
	CHECK(out->jumps[0].jumpPoint == "Halcyon Gate");
	CHECK(out->jumps[0].destination == 1);
	CHECK(out->jumps[0].arrival == "Vesper Gate");
	CHECK(out->jumps[1].jumpPoint == "Vesper Deep Gate");
	CHECK(out->jumps[1].destination == 2);
	CHECK(out->jumps[1].arrival == "Marrow Gate");
	// Each with its lane's toll.
	CHECK(out->jumps[0].toll == 0);
	CHECK(out->jumps[1].toll == 6000);

	const auto back = Space::PlotRoute(galaxy, {2, ""}, {0, ""});
	REQUIRE(back);
	CHECK(back->seconds == 105.0);
	REQUIRE(back->jumps.size() == 2);
	CHECK(back->jumps[0].jumpPoint == "Marrow Gate");
	CHECK(back->jumps[1].jumpPoint == "Vesper Gate");
	CHECK(back->jumps[1].arrival == "Halcyon Gate");

	const auto home = Space::PlotRoute(galaxy, {1, "Corva Refinery"}, {1, ""});
	REQUIRE(home);
	CHECK(home->jumps.empty());
	CHECK(home->seconds == 0.0);
}


TEST_CASE("Routes: two jumps against one, and the crossing between them weighed")
{
	// A to C is 20 s through B, or 30 s straight.
	const Celestial::Galaxy galaxy = Triangle(10.0, 10.0, 30.0);
	const auto lanesOnly = Space::PlotRoute(galaxy, {0, ""}, {2, ""});
	REQUIRE(lanesOnly);
	CHECK(lanesOnly->seconds == 20.0);
	REQUIRE(lanesOnly->jumps.size() == 2);
	CHECK(lanesOnly->jumps[0].jumpPoint == "A to B");
	CHECK(lanesOnly->jumps[1].jumpPoint == "B to C");

	// Crossing B from one gate to the other takes 15 s, which makes the way through B 35 s.
	const Space::CrossingCost slowB = [](std::size_t system, std::string_view, std::string_view)
	{ return system == 1 ? 15.0 : 0.0; };
	const auto direct = Space::PlotRoute(galaxy, {0, ""}, {2, ""}, slowB);
	REQUIRE(direct);
	CHECK(direct->seconds == 30.0);
	REQUIRE(direct->jumps.size() == 1);
	CHECK(direct->jumps[0].jumpPoint == "A to C");
	CHECK(direct->jumps[0].arrival == "C to A");

	// From a station in A whose gate to C is far from it and from the gate to B, which is close by:
	// 1 + 10 + 15 + 10 through B, against 50 + 30 straight, or 1 + 50 + 30 by way of the gate to B.
	const Space::CrossingCost station =
	    [](std::size_t system, std::string_view from, std::string_view to)
	{
		if (system == 0)
			return from == "A to C" || to == "A to C" ? 50.0 : 1.0;
		return system == 1 ? 15.0 : 0.0;
	};
	const auto fromStation = Space::PlotRoute(galaxy, {0, "Station"}, {2, ""}, station);
	REQUIRE(fromStation);
	CHECK(fromStation->seconds == 36.0);
	REQUIRE(fromStation->jumps.size() == 2);
	CHECK(fromStation->jumps[0].jumpPoint == "A to B");
}


TEST_CASE("Routes: a crossing is asked only between two bodies, and each route is the same again")
{
	const Celestial::Galaxy galaxy = Triangle(10.0, 10.0, 30.0);
	std::vector<std::string> asked;
	const Space::CrossingCost record =
	    [&](std::size_t system, std::string_view from, std::string_view to)
	{
		CHECK_FALSE(from.empty());
		CHECK_FALSE(to.empty());
		CHECK(from != to);
		asked.push_back(std::to_string(system) + ':' + std::string{from} + '>' + std::string{to});
		return 1.0;
	};
	const auto route = Space::PlotRoute(galaxy, {0, ""}, {2, "Station"}, record);
	REQUIRE(route);
	const std::vector<std::string> first = asked;
	asked.clear();
	const auto again = Space::PlotRoute(galaxy, {0, ""}, {2, "Station"}, record);
	REQUIRE(again);
	CHECK(asked == first);
	CHECK(again->seconds == route->seconds);
	REQUIRE(again->jumps.size() == route->jumps.size());
	for (std::size_t jump = 0; jump < route->jumps.size(); ++jump)
		CHECK(again->jumps[jump].lane == route->jumps[jump].lane);
}


TEST_CASE("Routes: a system no lane reaches has no route, and one the galaxy lacks is refused")
{
	const Celestial::Galaxy galaxy = Triangle(10.0, 10.0, 30.0);
	CHECK_FALSE(Space::PlotRoute(galaxy, {0, ""}, {3, ""}));
	CHECK_FALSE(Space::PlotRoute(galaxy, {3, ""}, {0, ""}));
	CHECK(Space::PlotRoute(galaxy, {3, ""}, {3, ""}));
	CHECK_THROWS_AS(Space::PlotRoute(galaxy, {0, ""}, {4, ""}), std::invalid_argument);
	const Space::CrossingCost negative = [](std::size_t, std::string_view, std::string_view)
	{ return -1.0; };
	CHECK_THROWS_AS(Space::PlotRoute(galaxy, {0, "Station"}, {2, ""}, negative), std::invalid_argument);
}


TEST_CASE("Courses: from Ostra Highport to Pell round Ostra when it is in the way")
{
	// The station circles Ostra every hour and a half, 420 km up, so over an orbit the planet is
	// sometimes between it and the moon and sometimes not.
	const Celestial::StarSystem halcyon = AiTests::Halcyon();
	constexpr double kClearance = 1.0e5;
	int blocked = 0;
	for (double time = 0.0; time < 5569.0; time += 600.0)
	{
		CAPTURE(time);
		const std::vector<Sphere> obstacles = Space::SystemObstacles(halcyon, time);
		REQUIRE(obstacles.size() == 3);
		const Ai::Waypoints waypoints{obstacles, kClearance};
		const auto at = [&](std::string_view name)
		{
			return Celestial::EvaluateBody(halcyon, static_cast<std::size_t>(halcyon.Find(name)), time)
			    .metres;
		};
		const glm::dvec3 station = at("Ostra Highport");
		const glm::dvec3 moon = at("Pell");
		const auto course = waypoints.PlotCourse(station, moon);
		REQUIRE(course);
		// The legs from the ends by the radii alone, and the end at Pell's centre not against Pell.
		const std::vector<Sphere> ostra{obstacles[1]};
		CHECK(Clear(*course, ostra, kClearance));
		CHECK(course->length == doctest::Approx(Length(*course)));
		const double straight = glm::length(moon - station);
		CHECK(course->length >= straight);
		if (!Ai::LegClear(station, moon, ostra, 0.0))
		{
			++blocked;
			CHECK(course->points.size() > 2);
			// Round Ostra's ring, 10 000 km from its centre, on a crossing of 384 000 km.
			CHECK(course->length <= straight + 2.0e7);
		}
		const auto again = waypoints.PlotCourse(station, moon);
		REQUIRE(again);
		CHECK(again->points == course->points);

		// The crossing cost is the course's length at the speed.
		const Space::CrossingCost crossing =
		    Space::CrossingByCourse({halcyon}, time, 1.0e3, kClearance);
		CHECK(crossing(0, "Ostra Highport", "Pell") == doctest::Approx(course->length / 1.0e3));
	}
	CHECK(blocked > 0);
}


TEST_CASE("Courses: a route across the galaxy at a cruising speed adds each crossing to the lanes")
{
	const Celestial::Galaxy galaxy = AiTests::Sample();
	const std::vector<Celestial::StarSystem> systems{
	    AiTests::Halcyon(), AiTests::Vesper(), AiTests::Marrow()};
	constexpr double kSpeed = 1.0e7;
	const Space::CrossingCost crossing = Space::CrossingByCourse(systems, 0.0, kSpeed, 1.0e5);
	const auto route = Space::PlotRoute(galaxy, {0, "Ostra Highport"}, {2, "Marrow Gate"}, crossing);
	REQUIRE(route);
	REQUIRE(route->jumps.size() == 2);
	const double expected = crossing(0, "Ostra Highport", "Halcyon Gate") + 40.0 +
	                        crossing(1, "Vesper Gate", "Vesper Deep Gate") + 65.0;
	CHECK(route->seconds == doctest::Approx(expected));

	CHECK_THROWS_AS(crossing(0, "Ostra Highport", "Nowhere"), std::invalid_argument);
	CHECK_THROWS_AS(crossing(5, "Ostra Highport", "Halcyon Gate"), std::invalid_argument);
	CHECK_THROWS_AS(Space::CrossingByCourse(systems, 0.0, 0.0, 1.0e5), std::invalid_argument);
}


TEST_CASE("Courses: a crossing costed by a time of the course's length, which has to be a time")
{
	const std::vector<Celestial::StarSystem> systems{
	    AiTests::Halcyon(), AiTests::Vesper(), AiTests::Marrow()};
	// The same course as at a speed, in the seconds the function gives for its length.
	const Space::CrossingCost bySpeed = Space::CrossingByCourse(systems, 0.0, 2.0e3, 1.0e5);
	const Space::CrossingCost byTime =
	    Space::CrossingByCourse(systems, 0.0, [](double metres) { return metres / 2.0e3; }, 1.0e5);
	CHECK(byTime(0, "Ostra Highport", "Pell") == doctest::Approx(bySpeed(0, "Ostra Highport", "Pell")));
	// A drive's time is not linear in the length: a fourth root of it, rising slower than the speed's.
	const Space::CruiseDrive drive{40.0f, 1.0e8, 0.25f};
	const Space::CrossingCost cruised = Space::CrossingByCourse(
	    systems, 0.0, [drive](double metres) { return Space::CruiseSeconds(drive, metres); }, 1.0e5);
	const double seconds = cruised(0, "Ostra Highport", "Pell");
	CHECK(seconds > 40.0);
	CHECK(seconds < 80.0);

	const auto asked = [&](Space::CourseTime time)
	{
		return Space::CrossingByCourse(systems, 0.0, std::move(time), 1.0e5)(
		    0, "Ostra Highport", "Pell");
	};
	CHECK_THROWS_AS(
	    Space::CrossingByCourse(systems, 0.0, Space::CourseTime{}, 1.0e5), std::invalid_argument);
	CHECK_THROWS_AS(asked([](double) { return -1.0; }), std::invalid_argument);
	CHECK_THROWS_AS(
	    asked([](double) { return std::numeric_limits<double>::infinity(); }), std::invalid_argument);
}
