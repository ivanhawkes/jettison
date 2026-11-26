#pragma once

#include <glm/vec3.hpp>

#include <optional>
#include <span>
#include <vector>

#include <jettison/ai/Pathfinding.h>

namespace Jettison::Ai
{
// Courses through a space at one moment, around the spheres in the way: A* (Pathfinding.h), and
// deterministic, so the same obstacles give the same course every run.

// Something a course goes round, where it is: a hill, a building, a planet.
struct Sphere
{
	glm::dvec3 centre{0.0};
	double radius{0.0};
};

// Whether a straight leg from a to b passes every sphere at its radius plus the clearance or
// further.
bool LegClear(
    const glm::dvec3 &a, const glm::dvec3 &b, std::span<const Sphere> obstacles, double clearance);

struct Course
{
	// From the start to the end, both included, with the waypoints between.
	std::vector<glm::dvec3> points;

	// In metres.
	double length{0.0};
};

// A graph of waypoints across a space at one moment, for courses around what is in the way.
// Each obstacle is ringed by six waypoints, at the corners of an octahedron about it 1.5 times its
// radius plus the clearance from its centre: a leg along one of the octahedron's edges then passes
// the obstacle with 6% to spare (an edge's middle is 1/√2 of the corner's distance from the centre),
// where 1/√2 would graze it and lose the leg to rounding. Two waypoints are joined wherever the
// straight leg between them is clear (LegClear), at its length. A waypoint inside another
// obstacle's clearance is joined to nothing.
//
// Courses are straight legs, so an obstacle that moves while a vehicle flies the course is where it
// was when the graph was made: a frame's bodies move slowly against a crossing, and a course is
// plotted again as it goes.
class Waypoints
{
  public:
	// The obstacles, the clearance in metres, and any waypoints of the caller's own, which are the
	// graph's first nodes, before the obstacles' rings. Throws std::invalid_argument for an obstacle
	// or a clearance that is negative or not finite.
	Waypoints(
	    std::vector<Sphere> obstacles, double clearance, std::vector<glm::dvec3> extraPoints = {});

	// The shortest course through the waypoints from one point to another, or nothing if every way
	// is blocked. A straight leg when it is clear. The legs to and from the ends are judged against
	// an obstacle by its radius alone when the end is within its clearance, so a course can leave a
	// body on rails in low orbit, and not at all when the end is inside the obstacle, so it can start
	// at a planet's centre. An end inside an obstacle's ring is given a waypoint of its own, straight
	// out from the obstacle's centre through the end at the ring's distance: the leg out to it moves
	// only away from that obstacle, and every direction is within 55° of one of the ring's corners,
	// near enough that the leg on to that corner clears the obstacle too (a chord of the ring's
	// sphere clears it up to 96°). Without it an end within a few thousandths of the obstacle's radius
	// and clearance, under the middle of one of the octahedron's faces, sees no corner past the limb.
	// A* with the straight-line distance to the end as its estimate.
	std::optional<Course> PlotCourse(const glm::dvec3 &from, const glm::dvec3 &to) const;

	std::span<const glm::dvec3> Points() const
	{
		return m_points;
	}

	const Graph &Legs() const
	{
		return m_graph;
	}

	std::span<const Sphere> Obstacles() const
	{
		return m_obstacles;
	}

	double Clearance() const
	{
		return m_clearance;
	}

  private:
	std::vector<Sphere> m_obstacles;
	double m_clearance;
	std::vector<glm::dvec3> m_points;
	Graph m_graph;
};

} // namespace Jettison::Ai
