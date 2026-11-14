#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/ai/Pathfinding.h>
#include <jettison/celestial/Galaxy.h>

namespace Jettison::Celestial
{
struct StarSystem;
}

namespace Jettison::Ai
{
// Routes across the galaxy, from a place in one star system to a place in another, over the jump
// lanes; and courses across one system, around the bodies in the way. Both are A* (Pathfinding.h),
// and both are deterministic: the same galaxy, systems and time give the same route every run.

// Where a route starts or ends: a body of a star system, by the system's index in the galaxy and the
// body's name (a station, a jump point), or, with no body, the system as a whole.
struct Place
{
	std::size_t system{0};
	std::string body;
};

// What it costs to cross a system from one of its bodies to another: from where a route starts, or
// the jump point it arrived at, to the jump point it leaves by, or to where it ends. In simulation
// seconds, as a lane's transit time is, and never negative. It is not asked about a place that is a
// system as a whole, which costs nothing to cross from or to.
using CrossingCost =
    std::function<double(std::size_t system, std::string_view from, std::string_view to)>;

struct Route
{
	// The lanes taken, in order, each seen from the end it is entered at; none for a route within
	// one system.
	std::vector<Celestial::Exit> jumps;
	// The lanes' transit times and the crossings between them.
	double seconds{0.0};
};

// The quickest route between two places, or nothing if no lanes join their systems. Its graph is
// the galaxy's jump points: a lane joins its two ends at its transit time, and within a system every
// jump point leads to every other at the crossing's cost, so a route that saves a jump by a long
// crossing is weighed against one that jumps twice. Without a crossing cost a route is the least
// transit time. A system's position in the galaxy says nothing of a lane's time or a crossing's, so
// the search has no estimate and is Dijkstra's. Throws std::invalid_argument for a system the galaxy
// does not have, and as FindPath does for a crossing that costs what is not a cost. The galaxy has to
// be resolved (Celestial::Resolve).
std::optional<Route> PlotRoute(
    const Celestial::Galaxy &galaxy, const Place &from, const Place &to,
    const CrossingCost &crossing = {});

// Something a course goes round: a star, a planet or a moon, where it is.
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

// A graph of waypoints across a star system at one moment, for courses around what is in the way.
// Each obstacle is ringed by six waypoints, at the corners of an octahedron about it 1.5 times its
// radius plus the clearance from its centre: a leg along one of the octahedron's edges then passes
// the obstacle with 6% to spare (an edge's middle is 1/√2 of the corner's distance from the centre),
// where 1/√2 would graze it and lose the leg to rounding. Two waypoints are joined wherever the
// straight leg between them is clear (LegClear), at its length. A waypoint inside another
// obstacle's clearance is joined to nothing.
//
// Courses are straight legs, so a body that moves along its orbit while a ship flies the course is
// where it was when the graph was made; the system's bodies move slowly against a crossing, and a
// course is plotted again as it goes.
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
	// station in low orbit, and not at all when the end is inside the obstacle, so it can start at a
	// planet's centre. An end inside an obstacle's ring is given a waypoint of its own, straight out
	// from the obstacle's centre through the end at the ring's distance: the leg out to it moves
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

// What is in a course's way in a star system at a time: its star, planets and moons, each a sphere
// of its radius where it is. Stations and jump points are what courses go to, and small beside
// what lies between them. The system has to be resolved.
std::vector<Sphere> SystemObstacles(const Celestial::StarSystem &system, double simSeconds);

// A crossing cost from courses: the length of the course between the two bodies, through the
// system's waypoints at the time (SystemObstacles, Waypoints) with that clearance, over the speed a
// ship crosses at, in metres a second. The systems are the galaxy's, in its order. A body the
// system does not have, or a crossing every course is blocked across, throws std::invalid_argument,
// as does a speed that is not positive and finite.
CrossingCost CrossingByCourse(
    std::vector<Celestial::StarSystem> systems, double simSeconds, double speed, double clearance);

// The seconds a course of that many metres takes, for a ship that does not cross at a speed: a
// cruise drive's time rises as the distance's fourth root (CruiseSeconds). Never negative, and
// zero for no distance.
using CourseTime = std::function<double(double metres)>;

// As above, with the seconds a course's length takes given by the caller, so a ship that cruises is
// costed by its drive. Throws std::invalid_argument for no function, and for a time that is negative
// or not finite, when a crossing is asked.
CrossingCost CrossingByCourse(
    std::vector<Celestial::StarSystem> systems, double simSeconds, CourseTime time, double clearance);
} // namespace Jettison::Ai
