#include <jettison/ai/Courses.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

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


// A point in the box's own axes, from its centre.
glm::dvec3 InBox(const Box &box, const glm::dvec3 &point)
{
	return glm::inverse(box.orientation) * (point - box.centre);
}


// How far a point is outside the box, 0 inside it.
double DistanceToBox(const Box &box, const glm::dvec3 &point)
{
	return glm::length(glm::max(glm::abs(InBox(box, point)) - box.half, glm::dvec3{0.0}));
}


// The margin a leg keeps from a box, as Keep's for a sphere: none judged (negative) for an end
// inside it, and for an end nearer it than the clearance no more than that end's own distance, a
// hair less so a leg level with the end is clear.
double Keep(const Box &box, double clearance, std::span<const glm::dvec3> ends)
{
	constexpr double kLevel = 1.0 - 1.0e-6;
	double keep = clearance;
	for (const glm::dvec3 &end : ends)
	{
		const double distance = DistanceToBox(box, end);
		if (distance <= 0.0)
			return -1.0;
		keep = std::min(keep, distance * kLevel);
	}
	return keep;
}


// Whether the leg passes into the box grown by the margin on every side: the slabs between each
// pair of its grown faces, met along the leg in its own axes. A leg along a face is not inside it.
bool LegIntoBox(const glm::dvec3 &a, const glm::dvec3 &b, const Box &box, double margin)
{
	const glm::dvec3 from = InBox(box, a);
	const glm::dvec3 along = InBox(box, b) - from;
	const glm::dvec3 reach = box.half + margin;
	double enter = 0.0;
	double leave = 1.0;
	for (glm::length_t axis = 0; axis < 3; ++axis)
	{
		if (along[axis] == 0.0)
		{
			if (std::abs(from[axis]) >= reach[axis])
				return false;
			continue;
		}
		double near = (-reach[axis] - from[axis]) / along[axis];
		double far = (reach[axis] - from[axis]) / along[axis];
		if (near > far)
			std::swap(near, far);
		enter = std::max(enter, near);
		leave = std::min(leave, far);
		if (enter >= leave)
			return false;
	}
	return true;
}


bool LegClearOfEnds(
    const glm::dvec3 &a, const glm::dvec3 &b, std::span<const Sphere> obstacles, double clearance,
    std::span<const glm::dvec3> ends, std::span<const Box> boxes)
{
	for (const Sphere &obstacle : obstacles)
	{
		const double keep = Keep(obstacle, clearance, ends);
		if (keep >= 0.0 && DistanceToSegment(a, b, obstacle.centre) < keep)
			return false;
	}
	for (const Box &box : boxes)
	{
		const double keep = Keep(box, clearance, ends);
		if (keep >= 0.0 && LegIntoBox(a, b, box, keep))
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
    const glm::dvec3 &a, const glm::dvec3 &b, std::span<const Sphere> obstacles, double clearance,
    std::span<const Box> boxes)
{
	return LegClearOfEnds(a, b, obstacles, clearance, {}, boxes);
}


Waypoints::Waypoints(
    std::vector<Sphere> obstacles, double clearance, std::vector<glm::dvec3> extraPoints,
    std::vector<Box> boxes)
    : m_obstacles{std::move(obstacles)}, m_boxes{std::move(boxes)}, m_clearance{clearance},
      m_points{std::move(extraPoints)}
{
	if (!std::isfinite(clearance) || clearance < 0.0)
		throw std::invalid_argument{
		    std::format("Ai: a clearance of {} m is not a distance", clearance)};
	for (const Sphere &obstacle : m_obstacles)
		if (!Finite(obstacle.centre) || !std::isfinite(obstacle.radius) || obstacle.radius < 0.0)
			throw std::invalid_argument{std::format(
			    "Ai: an obstacle of radius {} m at ({}, {}, {}) is not a sphere", obstacle.radius,
			    obstacle.centre.x, obstacle.centre.y, obstacle.centre.z)};
	for (const Box &box : m_boxes)
		if (!Finite(box.centre) || !Finite(box.half) || box.half.x < 0.0 || box.half.y < 0.0 ||
		    box.half.z < 0.0 || !std::isfinite(box.orientation.w) ||
		    !std::isfinite(box.orientation.x) || !std::isfinite(box.orientation.y) ||
		    !std::isfinite(box.orientation.z))
			throw std::invalid_argument{std::format(
			    "Ai: a box at ({}, {}, {}) is not a box", box.centre.x, box.centre.y, box.centre.z)};
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
	for (const Box &box : m_boxes)
	{
		const glm::dvec3 ring = box.half + kRingScale * clearance;
		for (const double x : {-1.0, 1.0})
			for (const double y : {-1.0, 1.0})
				for (const double z : {-1.0, 1.0})
					m_points.push_back(box.centre + box.orientation * (ring * glm::dvec3{x, y, z}));
	}
	m_graph.adjacency.resize(m_points.size());
	for (std::size_t a = 0; a < m_points.size(); ++a)
		for (std::size_t b = a + 1; b < m_points.size(); ++b)
			if (LegClear(m_points[a], m_points[b], m_obstacles, m_clearance, m_boxes))
				Join(
				    m_graph, static_cast<NodeId>(a), static_cast<NodeId>(b),
				    glm::length(m_points[b] - m_points[a]));
}


std::optional<Course> Waypoints::PlotCourse(const glm::dvec3 &from, const glm::dvec3 &to) const
{
	if (!Finite(from) || !Finite(to))
		throw std::invalid_argument{"Ai: a course from or to nowhere"};
	const std::array<glm::dvec3, 2> both{from, to};
	if (LegClearOfEnds(from, to, m_obstacles, m_clearance, both, m_boxes))
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
	for (const glm::dvec3 &point : both)
		for (const Box &box : m_boxes)
		{
			// Inside the box's ring and not in the box: straight out from each face to the ring.
			const glm::dvec3 local = InBox(box, point);
			const glm::dvec3 ring = box.half + kRingScale * m_clearance;
			if (DistanceToBox(box, point) <= 0.0 ||
			    glm::any(glm::greaterThanEqual(glm::abs(local), ring)))
				continue;
			for (glm::length_t axis = 0; axis < 3; ++axis)
				for (const double side : {-1.0, 1.0})
				{
					glm::dvec3 out = local;
					out[axis] = side * ring[axis];
					points.push_back(box.centre + box.orientation * out);
				}
		}
	const auto start = static_cast<NodeId>(points.size());
	const NodeId goal = start + 1;
	points.push_back(from);
	points.push_back(to);

	Graph graph = m_graph;
	graph.adjacency.resize(points.size());
	for (std::size_t escape = m_points.size(); escape < start; ++escape)
		for (std::size_t other = 0; other < escape; ++other)
			if (LegClear(points[escape], points[other], m_obstacles, m_clearance, m_boxes))
				Join(
				    graph, static_cast<NodeId>(escape), static_cast<NodeId>(other),
				    glm::length(points[other] - points[escape]));
	for (const NodeId node : {start, goal})
	{
		const glm::dvec3 &at = points[node];
		const std::array<glm::dvec3, 1> self{at};
		for (NodeId other = 0; other < start; ++other)
			if (LegClearOfEnds(at, points[other], m_obstacles, m_clearance, self, m_boxes))
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
