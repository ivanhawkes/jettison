#include <jettison/ai/Courses.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

using namespace Jettison;
using Ai::Sphere;

namespace
{
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


TEST_CASE("Courses: a box in the way is gone round by its corners, by the clearance at least")
{
	// A deck 40 m across and 4 m thick, turned a quarter about y, which a square box looks the same
	// for, and a clearance of 2 m.
	const std::vector<Ai::Box> deck{
	    {glm::dvec3{0.0},
	     {20.0, 2.0, 20.0},
	     glm::angleAxis(glm::half_pi<double>(), glm::dvec3{0, 1, 0})}};
	constexpr double kClearance = 2.0;
	CHECK_FALSE(Ai::LegClear({-30.0, 0.0, 0.0}, {30.0, 0.0, 0.0}, {}, kClearance, deck));
	CHECK(Ai::LegClear({-30.0, 4.5, 0.0}, {30.0, 4.5, 0.0}, {}, kClearance, deck));
	CHECK_FALSE(Ai::LegClear({-30.0, 3.5, 0.0}, {30.0, 3.5, 0.0}, {}, kClearance, deck));

	const Ai::Waypoints waypoints{{}, kClearance, {}, deck};
	REQUIRE(waypoints.Points().size() == 8);
	const auto round = waypoints.PlotCourse({-30.0, 0.0, 0.0}, {30.0, 0.0, 0.0});
	REQUIRE(round);
	REQUIRE(round->points.size() > 2);
	for (std::size_t leg = 2; leg + 1 < round->points.size(); ++leg)
		CHECK(Ai::LegClear(round->points[leg - 1], round->points[leg], {}, kClearance, deck));

	// The same every time.
	const auto again = waypoints.PlotCourse({-30.0, 0.0, 0.0}, {30.0, 0.0, 0.0});
	REQUIRE(again);
	CHECK(again->points == round->points);
}


TEST_CASE(
    "Courses: from just over a deck under a pier, a course goes out from under it, as near as it began")
{
	// A deck whose top is at y = 0, and a pier over it from y = 6 to 8 reaching to x = 30; the point
	// 3 m over the deck under the pier is nearer both than the clearance.
	const std::vector<Ai::Box> structure{
	    {{0.0, -2.0, 0.0}, {40.0, 2.0, 40.0}}, {{15.0, 7.0, 0.0}, {15.0, 1.0, 10.0}}};
	constexpr double kClearance = 5.0;
	const glm::dvec3 under{20.0, 3.0, 0.0};
	const glm::dvec3 away{0.0, 60.0, 0.0};
	const Ai::Waypoints waypoints{{}, kClearance, {}, structure};
	const auto out = waypoints.PlotCourse(away, under);
	REQUIRE(out);
	REQUIRE(out->points.size() > 2);
	CHECK(out->points.back() == under);

	// No leg meets the structure, and the last, from under the pier, comes no nearer it than the end
	// is: 3 m from the deck and the pier.
	for (std::size_t leg = 1; leg < out->points.size(); ++leg)
		CHECK(Ai::LegClear(out->points[leg - 1], out->points[leg], {}, 0.0, structure));
	CHECK(Ai::LegClear(out->points[out->points.size() - 2], under, {}, 2.9, structure));
}
