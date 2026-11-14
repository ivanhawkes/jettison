#include <jettison/ai/Routes.h>

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

namespace Jettison::Ai
{
namespace
{
// How far out an obstacle's ring of waypoints is, as a multiple of its radius plus the clearance.
constexpr double kRingScale = 1.5;

bool Finite(const glm::dvec3 &value)
{
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double DistanceToSegment(const glm::dvec3 &a, const glm::dvec3 &b, const glm::dvec3 &point)
{
	const glm::dvec3 along = b - a;
	const double lengthSquared = glm::dot(along, along);
	const double t =
	    lengthSquared > 0.0 ? std::clamp(glm::dot(point - a, along) / lengthSquared, 0.0, 1.0) : 0.0;
	return glm::length(a + along * t - point);
}

// The radius a leg has to keep outside of an obstacle, given the ends of it that are a course's
// ends: none for an obstacle an end is inside, the radius alone for one whose clearance holds an
// end, and the radius and the clearance otherwise. Negative means the obstacle is not judged.
double Keep(const Sphere &obstacle, double clearance, std::span<const glm::dvec3> ends)
{
	double keep = obstacle.radius + clearance;
	for (const glm::dvec3 &end : ends)
	{
		const double distance = glm::length(end - obstacle.centre);
		if (distance < obstacle.radius)
			return -1.0;
		if (distance < obstacle.radius + clearance)
			keep = obstacle.radius;
	}
	return keep;
}

bool LegClearOfEnds(
    const glm::dvec3 &a, const glm::dvec3 &b, std::span<const Sphere> obstacles, double clearance,
    std::span<const glm::dvec3> ends)
{
	for (const Sphere &obstacle : obstacles)
	{
		const double keep = Keep(obstacle, clearance, ends);
		if (keep >= 0.0 && DistanceToSegment(a, b, obstacle.centre) < keep)
			return false;
	}
	return true;
}

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

bool LegClear(
    const glm::dvec3 &a, const glm::dvec3 &b, std::span<const Sphere> obstacles, double clearance)
{
	return LegClearOfEnds(a, b, obstacles, clearance, {});
}

Waypoints::Waypoints(
    std::vector<Sphere> obstacles, double clearance, std::vector<glm::dvec3> extraPoints)
    : m_obstacles{std::move(obstacles)}, m_clearance{clearance}, m_points{std::move(extraPoints)}
{
	if (!std::isfinite(clearance) || clearance < 0.0)
		throw std::invalid_argument{
		    std::format("Ai: a clearance of {} m is not a distance", clearance)};
	for (const Sphere &obstacle : m_obstacles)
		if (!Finite(obstacle.centre) || !std::isfinite(obstacle.radius) || obstacle.radius < 0.0)
			throw std::invalid_argument{std::format(
			    "Ai: an obstacle of radius {} m at ({}, {}, {}) is not a sphere", obstacle.radius,
			    obstacle.centre.x, obstacle.centre.y, obstacle.centre.z)};
	for (const glm::dvec3 &point : m_points)
		if (!Finite(point))
			throw std::invalid_argument{
			    std::format("Ai: a waypoint at ({}, {}, {}) is nowhere", point.x, point.y, point.z)};

	for (const Sphere &obstacle : m_obstacles)
	{
		const double ring = kRingScale * (obstacle.radius + clearance);
		for (int axis = 0; axis < 3; ++axis)
			for (const double side : {1.0, -1.0})
			{
				glm::dvec3 corner = obstacle.centre;
				corner[axis] += side * ring;
				m_points.push_back(corner);
			}
	}
	m_graph.adjacency.resize(m_points.size());
	for (std::size_t a = 0; a < m_points.size(); ++a)
		for (std::size_t b = a + 1; b < m_points.size(); ++b)
			if (LegClear(m_points[a], m_points[b], m_obstacles, m_clearance))
				Join(
				    m_graph, static_cast<NodeId>(a), static_cast<NodeId>(b),
				    glm::length(m_points[b] - m_points[a]));
}

std::optional<Course> Waypoints::PlotCourse(const glm::dvec3 &from, const glm::dvec3 &to) const
{
	if (!Finite(from) || !Finite(to))
		throw std::invalid_argument{"Ai: a course from or to nowhere"};
	const std::array<glm::dvec3, 2> both{from, to};
	if (LegClearOfEnds(from, to, m_obstacles, m_clearance, both))
		return Course{{from, to}, glm::length(to - from)};

	// The graph's waypoints, then each end's escapes from the rings it is inside, then the ends.
	std::vector<glm::dvec3> points{m_points.begin(), m_points.end()};
	for (const glm::dvec3 &point : both)
		for (const Sphere &obstacle : m_obstacles)
		{
			const glm::dvec3 out = point - obstacle.centre;
			const double distance = glm::length(out);
			const double ring = kRingScale * (obstacle.radius + m_clearance);
			if (distance > 0.0 && distance < ring)
				points.push_back(obstacle.centre + out * (ring / distance));
		}
	const auto start = static_cast<NodeId>(points.size());
	const NodeId goal = start + 1;
	points.push_back(from);
	points.push_back(to);

	Graph graph = m_graph;
	graph.adjacency.resize(points.size());
	for (std::size_t escape = m_points.size(); escape < start; ++escape)
		for (std::size_t other = 0; other < escape; ++other)
			if (LegClear(points[escape], points[other], m_obstacles, m_clearance))
				Join(
				    graph, static_cast<NodeId>(escape), static_cast<NodeId>(other),
				    glm::length(points[other] - points[escape]));
	for (const NodeId node : {start, goal})
	{
		const glm::dvec3 &at = points[node];
		const std::array<glm::dvec3, 1> self{at};
		for (NodeId other = 0; other < start; ++other)
			if (LegClearOfEnds(at, points[other], m_obstacles, m_clearance, self))
				Join(graph, node, other, glm::length(points[other] - at));
	}

	const std::optional<Path> path =
	    FindPath(graph, start, goal, [&](NodeId node) { return glm::length(to - points[node]); });
	if (!path)
		return std::nullopt;
	Course course{{}, path->cost};
	for (const NodeId node : path->nodes)
		course.points.push_back(points[node]);
	return course;
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
} // namespace Jettison::Ai
