#include <jettison/ai/Courses.h>

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
} // namespace Jettison::Ai
