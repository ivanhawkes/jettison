#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/ai/Courses.h>
#include <jettison/celestial/Galaxy.h>

namespace Jettison::Celestial
{
struct StarSystem;
}

namespace Jettison::Space
{
// Routes across the galaxy, from a place in one star system to a place in another, over the jump
// lanes, and what crossing a system costs, by courses around its bodies (ai/Courses.h). Both are A*
// (Pathfinding.h), and both are deterministic: the same galaxy, systems and time give the same route
// every run.

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

// What is in a course's way in a star system at a time: its star, planets and moons, each a sphere
// of its radius where it is. Stations and jump points are what courses go to, and small beside
// what lies between them. The system has to be resolved.
std::vector<Ai::Sphere> SystemObstacles(const Celestial::StarSystem &system, double simSeconds);

// A crossing cost from courses: the length of the course between the two bodies, through the
// system's waypoints at the time (SystemObstacles, Ai::Waypoints) with that clearance, over the speed a
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
} // namespace Jettison::Space
