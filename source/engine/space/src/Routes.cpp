#include <jettison/space/Routes.h>

#include <jettison/celestial/Frames.h>
#include <jettison/celestial/StarSystem.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Jettison::Space
{
using Ai::Course;
using Ai::FindPath;
using Ai::Graph;
using Ai::NodeId;
using Ai::Path;
using Ai::Sphere;
using Ai::Waypoints;

namespace
{
void Join(Graph &graph, NodeId a, NodeId b, double cost)
{
	graph.adjacency[a].push_back({b, cost});
	graph.adjacency[b].push_back({a, cost});
}
} // namespace


std::optional<Route> PlotRoute(
    const Celestial::Galaxy &galaxy, const Place &from, const Place &to, const CrossingCost &crossing)
{
	const std::size_t systems = galaxy.systems.size();
	if (from.system >= systems || to.system >= systems)
		throw std::invalid_argument{std::format(
		    "Ai: a route from system {} to system {} in a galaxy of {} systems", from.system, to.system,
		    systems)};

	// Node 0 is where the route starts and node 1 where it ends; each lane's two ends follow.
	constexpr NodeId kFrom = 0;
	constexpr NodeId kTo = 1;
	const auto end = [](std::size_t lane, bool b) { return static_cast<NodeId>(2 + (2 * lane) + b); };
	const auto endOf = [&](NodeId node) -> const Celestial::LaneEnd &
	{
		const Celestial::JumpLane &lane = galaxy.lanes[(node - 2) / 2];
		return (node - 2) % 2 == 0 ? lane.a : lane.b;
	};
	const auto cost = [&](std::size_t system, std::string_view a, std::string_view b)
	{ return !crossing || a.empty() || b.empty() || a == b ? 0.0 : crossing(system, a, b); };

	Graph graph;
	graph.adjacency.resize(2 + (2 * galaxy.lanes.size()));
	std::vector<std::vector<NodeId>> ends(systems);
	for (std::size_t lane = 0; lane < galaxy.lanes.size(); ++lane)
	{
		Join(graph, end(lane, false), end(lane, true), galaxy.lanes[lane].transitSeconds);
		ends[galaxy.lanes[lane].a.systemIndex].push_back(end(lane, false));
		ends[galaxy.lanes[lane].b.systemIndex].push_back(end(lane, true));
	}
	for (std::size_t system = 0; system < systems; ++system)
		for (const NodeId a : ends[system])
			for (const NodeId b : ends[system])
				if (a != b)
					graph.adjacency[a].push_back(
					    {b, cost(system, endOf(a).jumpPoint, endOf(b).jumpPoint)});
	for (const NodeId a : ends[from.system])
		graph.adjacency[kFrom].push_back({a, cost(from.system, from.body, endOf(a).jumpPoint)});
	for (const NodeId b : ends[to.system])
		graph.adjacency[b].push_back({kTo, cost(to.system, endOf(b).jumpPoint, to.body)});
	if (from.system == to.system)
		graph.adjacency[kFrom].push_back({kTo, cost(from.system, from.body, to.body)});

	const std::optional<Path> path = FindPath(graph, kFrom, kTo);
	if (!path)
		return std::nullopt;
	Route route{{}, path->cost};
	for (std::size_t step = 1; step < path->nodes.size(); ++step)
	{
		const NodeId a = path->nodes[step - 1];
		const NodeId b = path->nodes[step];
		if (a < 2 || b < 2 || (a - 2) / 2 != (b - 2) / 2)
			continue;
		const std::size_t lane = (a - 2) / 2;
		route.jumps.push_back(
		    {lane, endOf(a).jumpPoint, endOf(b).systemIndex, endOf(b).jumpPoint,
		     galaxy.lanes[lane].transitSeconds, galaxy.lanes[lane].toll});
	}
	return route;
}


std::vector<Sphere> SystemObstacles(const Celestial::StarSystem &system, double simSeconds)
{
	const std::vector<Celestial::SystemPosition> positions =
	    Celestial::EvaluateSystem(system, simSeconds);
	std::vector<Sphere> obstacles;
	for (std::size_t body = 0; body < system.bodies.size(); ++body)
	{
		const Celestial::Body &celestial = system.bodies[body];
		const bool inTheWay = celestial.kind == Celestial::BodyKind::Star ||
		                      celestial.kind == Celestial::BodyKind::Planet ||
		                      celestial.kind == Celestial::BodyKind::Moon;
		if (inTheWay && celestial.radius > 0.0)
			obstacles.push_back({positions[body].metres, celestial.radius});
	}
	return obstacles;
}


CrossingCost CrossingByCourse(
    std::vector<Celestial::StarSystem> systems, double simSeconds, double speed, double clearance)
{
	if (!std::isfinite(speed) || speed <= 0.0)
		throw std::invalid_argument{std::format("Ai: a crossing at {} m/s is no crossing", speed)};
	return CrossingByCourse(
	    std::move(systems), simSeconds, [speed](double metres) { return metres / speed; }, clearance);
}


CrossingCost CrossingByCourse(
    std::vector<Celestial::StarSystem> systems, double simSeconds, CourseTime time, double clearance)
{
	if (!time)
		throw std::invalid_argument{"Ai: a crossing has no time to take"};
	struct Crossings
	{
		std::vector<Celestial::StarSystem> systems;
		std::vector<std::vector<Celestial::SystemPosition>> positions;
		std::vector<Waypoints> waypoints;
	};
	auto crossings = std::make_shared<Crossings>();
	for (const Celestial::StarSystem &system : systems)
	{
		crossings->positions.push_back(Celestial::EvaluateSystem(system, simSeconds));
		crossings->waypoints.emplace_back(SystemObstacles(system, simSeconds), clearance);
	}
	crossings->systems = std::move(systems);
	return [crossings,
	        time = std::move(time)](std::size_t system, std::string_view from, std::string_view to)
	{
		if (system >= crossings->systems.size())
			throw std::invalid_argument{std::format(
			    "Ai: a crossing of system {}, of {} given", system, crossings->systems.size())};
		const Celestial::StarSystem &starSystem = crossings->systems[system];
		const int a = starSystem.Find(from);
		const int b = starSystem.Find(to);
		if (a < 0 || b < 0)
			throw std::invalid_argument{std::format(
			    "Ai: a crossing of '{}' from '{}' to '{}', and it has no '{}'", starSystem.name, from,
			    to, a < 0 ? from : to)};
		const std::vector<Celestial::SystemPosition> &at = crossings->positions[system];
		const std::optional<Course> course = crossings->waypoints[system].PlotCourse(
		    at[static_cast<std::size_t>(a)].metres, at[static_cast<std::size_t>(b)].metres);
		if (!course)
			throw std::invalid_argument{std::format(
			    "Ai: every course across '{}' from '{}' to '{}' is blocked", starSystem.name, from,
			    to)};
		const double seconds = time(course->length);
		if (!std::isfinite(seconds) || seconds < 0.0)
			throw std::invalid_argument{std::format(
			    "Ai: a course of {} m across '{}' takes {} s", course->length, starSystem.name,
			    seconds)};
		return seconds;
	};
}
} // namespace Jettison::Space
