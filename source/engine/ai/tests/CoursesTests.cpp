#include <jettison/ai/Courses.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

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
