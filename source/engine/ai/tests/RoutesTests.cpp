#include "Systems.h"

#include <jettison/ai/Cruise.h>
#include <jettison/ai/Routes.h>
#include <jettison/celestial/Frames.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using Ai::Place;
using Ai::Sphere;

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

	const auto out = Ai::PlotRoute(galaxy, {0, ""}, {2, ""});
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

	const auto back = Ai::PlotRoute(galaxy, {2, ""}, {0, ""});
	REQUIRE(back);
	CHECK(back->seconds == 105.0);
	REQUIRE(back->jumps.size() == 2);
	CHECK(back->jumps[0].jumpPoint == "Marrow Gate");
	CHECK(back->jumps[1].jumpPoint == "Vesper Gate");
	CHECK(back->jumps[1].arrival == "Halcyon Gate");

	const auto home = Ai::PlotRoute(galaxy, {1, "Corva Refinery"}, {1, ""});
	REQUIRE(home);
	CHECK(home->jumps.empty());
	CHECK(home->seconds == 0.0);
}

TEST_CASE("Routes: two jumps against one, and the crossing between them weighed")
{
	// A to C is 20 s through B, or 30 s straight.
	const Celestial::Galaxy galaxy = Triangle(10.0, 10.0, 30.0);
	const auto lanesOnly = Ai::PlotRoute(galaxy, {0, ""}, {2, ""});
	REQUIRE(lanesOnly);
	CHECK(lanesOnly->seconds == 20.0);
	REQUIRE(lanesOnly->jumps.size() == 2);
	CHECK(lanesOnly->jumps[0].jumpPoint == "A to B");
	CHECK(lanesOnly->jumps[1].jumpPoint == "B to C");

	// Crossing B from one gate to the other takes 15 s, which makes the way through B 35 s.
	const Ai::CrossingCost slowB = [](std::size_t system, std::string_view, std::string_view)
	{ return system == 1 ? 15.0 : 0.0; };
	const auto direct = Ai::PlotRoute(galaxy, {0, ""}, {2, ""}, slowB);
	REQUIRE(direct);
	CHECK(direct->seconds == 30.0);
	REQUIRE(direct->jumps.size() == 1);
	CHECK(direct->jumps[0].jumpPoint == "A to C");
	CHECK(direct->jumps[0].arrival == "C to A");

	// From a station in A whose gate to C is far from it and from the gate to B, which is close by:
	// 1 + 10 + 15 + 10 through B, against 50 + 30 straight, or 1 + 50 + 30 by way of the gate to B.
	const Ai::CrossingCost station = [](std::size_t system, std::string_view from, std::string_view to)
	{
		if (system == 0)
			return from == "A to C" || to == "A to C" ? 50.0 : 1.0;
		return system == 1 ? 15.0 : 0.0;
	};
	const auto fromStation = Ai::PlotRoute(galaxy, {0, "Station"}, {2, ""}, station);
	REQUIRE(fromStation);
	CHECK(fromStation->seconds == 36.0);
	REQUIRE(fromStation->jumps.size() == 2);
	CHECK(fromStation->jumps[0].jumpPoint == "A to B");
}

TEST_CASE("Routes: a crossing is asked only between two bodies, and each route is the same again")
{
	const Celestial::Galaxy galaxy = Triangle(10.0, 10.0, 30.0);
	std::vector<std::string> asked;
	const Ai::CrossingCost record = [&](std::size_t system, std::string_view from, std::string_view to)
	{
		CHECK_FALSE(from.empty());
		CHECK_FALSE(to.empty());
		CHECK(from != to);
		asked.push_back(std::to_string(system) + ':' + std::string{from} + '>' + std::string{to});
		return 1.0;
	};
	const auto route = Ai::PlotRoute(galaxy, {0, ""}, {2, "Station"}, record);
	REQUIRE(route);
	const std::vector<std::string> first = asked;
	asked.clear();
	const auto again = Ai::PlotRoute(galaxy, {0, ""}, {2, "Station"}, record);
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
	CHECK_FALSE(Ai::PlotRoute(galaxy, {0, ""}, {3, ""}));
	CHECK_FALSE(Ai::PlotRoute(galaxy, {3, ""}, {0, ""}));
	CHECK(Ai::PlotRoute(galaxy, {3, ""}, {3, ""}));
	CHECK_THROWS_AS(Ai::PlotRoute(galaxy, {0, ""}, {4, ""}), std::invalid_argument);
	const Ai::CrossingCost negative = [](std::size_t, std::string_view, std::string_view)
	{ return -1.0; };
	CHECK_THROWS_AS(Ai::PlotRoute(galaxy, {0, "Station"}, {2, ""}, negative), std::invalid_argument);
}

TEST_CASE("Courses: a clear leg is straight, and a planet in the way is gone round by its ring")
{
	const std::vector<Sphere> planet{{glm::dvec3{0.0}, 1.0}};
	const Ai::Waypoints waypoints{planet, 0.0};
	REQUIRE(waypoints.Points().size() == 6);

	const auto past = waypoints.PlotCourse({-10.0, 2.0, 0.0}, {10.0, 2.0, 0.0});
	REQUIRE(past);
	CHECK(past->points.size() == 2);
	CHECK(past->length == 20.0);

	// Straight through the middle: the way round is over any of the four corners square to the
	// line, 1.5 from the centre, each the same length; the first is the corner along +y.
	const glm::dvec3 from{-10.0, 0.0, 0.0};
	const glm::dvec3 to{10.0, 0.0, 0.0};
	CHECK_FALSE(Ai::LegClear(from, to, planet, 0.0));
	const auto round = waypoints.PlotCourse(from, to);
	REQUIRE(round);
	REQUIRE(round->points.size() == 3);
	CHECK(round->points[1] == glm::dvec3{0.0, 1.5, 0.0});
	CHECK(round->length == doctest::Approx(2.0 * std::sqrt(100.0 + 2.25)));
	CHECK(round->length == doctest::Approx(Length(*round)));
	CHECK(Clear(*round, planet, 0.0));
	// No shorter than going round the sphere itself: the tangents from each end and the arc between.
	const double tangent = std::sqrt(100.0 - 1.0);
	const double arc = std::numbers::pi - (2.0 * std::acos(1.0 / 10.0));
	CHECK(round->length >= (2.0 * tangent) + arc);
}

TEST_CASE("Courses: a clearance keeps a course off a planet, and every way blocked is none")
{
	const std::vector<Sphere> planet{{glm::dvec3{0.0}, 1.0}};
	// Passing 2 from the centre is clear by 1, not by a clearance of 1.5.
	const glm::dvec3 from{-10.0, 2.0, 0.0};
	const glm::dvec3 to{10.0, 2.0, 0.0};
	CHECK(Ai::LegClear(from, to, planet, 0.5));
	CHECK_FALSE(Ai::LegClear(from, to, planet, 1.5));
	const Ai::Waypoints wide{planet, 1.5};
	const auto course = wide.PlotCourse(from, to);
	REQUIRE(course);
	CHECK(course->points.size() > 2);
	CHECK(Clear(*course, planet, 1.5));

	// A shell of planets with a ship inside it, each ring blocked by its neighbours.
	std::vector<Sphere> shell;
	for (int axis = 0; axis < 3; ++axis)
		for (const double side : {1.0, -1.0})
		{
			glm::dvec3 centre{0.0};
			centre[axis] = side * 3.0;
			shell.push_back({centre, 2.9});
		}
	const Ai::Waypoints trapped{shell, 0.0};
	CHECK_FALSE(trapped.PlotCourse(glm::dvec3{0.0}, {100.0, 0.0, 0.0}));
}

TEST_CASE("Courses: an end just above a planet, under a face of its ring, still finds a way out")
{
	// Above the middle of the face between the +x, +y and +z corners, a thousandth of its radius
	// over the surface: no corner is in sight past the limb, so the course leaves straight up to its
	// own waypoint first. At a hundredth the nearest corner is in sight, just.
	const std::vector<Sphere> planet{{glm::dvec3{0.0}, 1.0}};
	const Ai::Waypoints waypoints{planet, 0.0};
	const glm::dvec3 above = glm::normalize(glm::dvec3{1.0}) * 1.001;
	for (const glm::dvec3 &corner : waypoints.Points())
		CHECK_FALSE(Ai::LegClear(above, corner, planet, 0.0));
	const auto course = waypoints.PlotCourse(above, -above);
	REQUIRE(course);
	CHECK(Clear(*course, planet, 0.0));
	CHECK(glm::length(course->points[1]) == doctest::Approx(1.5));
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
		const std::vector<Sphere> obstacles = Ai::SystemObstacles(halcyon, time);
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
		const Ai::CrossingCost crossing = Ai::CrossingByCourse({halcyon}, time, 1.0e3, kClearance);
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
	const Ai::CrossingCost crossing = Ai::CrossingByCourse(systems, 0.0, kSpeed, 1.0e5);
	const auto route = Ai::PlotRoute(galaxy, {0, "Ostra Highport"}, {2, "Marrow Gate"}, crossing);
	REQUIRE(route);
	REQUIRE(route->jumps.size() == 2);
	const double expected = crossing(0, "Ostra Highport", "Halcyon Gate") + 40.0 +
	                        crossing(1, "Vesper Gate", "Vesper Deep Gate") + 65.0;
	CHECK(route->seconds == doctest::Approx(expected));

	CHECK_THROWS_AS(crossing(0, "Ostra Highport", "Nowhere"), std::invalid_argument);
	CHECK_THROWS_AS(crossing(5, "Ostra Highport", "Halcyon Gate"), std::invalid_argument);
	CHECK_THROWS_AS(Ai::CrossingByCourse(systems, 0.0, 0.0, 1.0e5), std::invalid_argument);
}

TEST_CASE("Courses: a crossing costed by a time of the course's length, which has to be a time")
{
	const std::vector<Celestial::StarSystem> systems{
	    AiTests::Halcyon(), AiTests::Vesper(), AiTests::Marrow()};
	// The same course as at a speed, in the seconds the function gives for its length.
	const Ai::CrossingCost bySpeed = Ai::CrossingByCourse(systems, 0.0, 2.0e3, 1.0e5);
	const Ai::CrossingCost byTime =
	    Ai::CrossingByCourse(systems, 0.0, [](double metres) { return metres / 2.0e3; }, 1.0e5);
	CHECK(byTime(0, "Ostra Highport", "Pell") == doctest::Approx(bySpeed(0, "Ostra Highport", "Pell")));
	// A drive's time is not linear in the length: a fourth root of it, rising slower than the speed's.
	const Ai::CruiseDrive drive{40.0f, 1.0e8, 0.25f};
	const Ai::CrossingCost cruised = Ai::CrossingByCourse(
	    systems, 0.0, [drive](double metres) { return Ai::CruiseSeconds(drive, metres); }, 1.0e5);
	const double seconds = cruised(0, "Ostra Highport", "Pell");
	CHECK(seconds > 40.0);
	CHECK(seconds < 80.0);

	const auto asked = [&](Ai::CourseTime time)
	{ return Ai::CrossingByCourse(systems, 0.0, std::move(time), 1.0e5)(0, "Ostra Highport", "Pell"); };
	CHECK_THROWS_AS(Ai::CrossingByCourse(systems, 0.0, Ai::CourseTime{}, 1.0e5), std::invalid_argument);
	CHECK_THROWS_AS(asked([](double) { return -1.0; }), std::invalid_argument);
	CHECK_THROWS_AS(
	    asked([](double) { return std::numeric_limits<double>::infinity(); }), std::invalid_argument);
}

TEST_CASE("Courses: an obstacle or a clearance that is not one is refused")
{
	const double nan = std::numeric_limits<double>::quiet_NaN();
	CHECK_THROWS_AS(Ai::Waypoints({{glm::dvec3{0.0}, -1.0}}, 0.0), std::invalid_argument);
	CHECK_THROWS_AS(Ai::Waypoints({{glm::dvec3{nan}, 1.0}}, 0.0), std::invalid_argument);
	CHECK_THROWS_AS(Ai::Waypoints({}, -1.0), std::invalid_argument);
	CHECK_THROWS_AS(Ai::Waypoints({}, 0.0, {glm::dvec3{nan}}), std::invalid_argument);
	const Ai::Waypoints none{{}, 0.0};
	CHECK_THROWS_AS((void)none.PlotCourse(glm::dvec3{nan}, glm::dvec3{0.0}), std::invalid_argument);
}
