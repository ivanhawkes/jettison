#include <jettison/render/DebugDraw.h>

#include <array>
#include <cmath>
#include <numbers>

#include <jettison/render/Camera.h>

namespace Jettison::Render
{
void DebugDraw::Line(
    const glm::dvec3 &from, const glm::dvec3 &to, const glm::vec4 &color, DebugDepth depth)
{
	List(depth).push_back({from, to, color});
}

void DebugDraw::Polyline(
    std::span<const glm::dvec3> points, const glm::vec4 &color, bool closed, DebugDepth depth)
{
	if (points.size() < 2)
		return;
	for (std::size_t index = 1; index < points.size(); ++index)
		Line(points[index - 1], points[index], color, depth);
	if (closed && points.size() > 2)
		Line(points.back(), points.front(), color, depth);
}

void DebugDraw::Circle(
    const glm::dvec3 &center, const glm::dvec3 &normal, double radius, const glm::vec4 &color,
    int segments, DebugDepth depth)
{
	if (segments < 3 || glm::length(normal) == 0.0)
		return;
	const glm::dvec3 axis = glm::normalize(normal);
	// Any vector not along the axis gives the circle's plane.
	const glm::dvec3 helper =
	    std::abs(axis.y) < 0.9 ? glm::dvec3{0.0, 1.0, 0.0} : glm::dvec3{1.0, 0.0, 0.0};
	const glm::dvec3 u = glm::normalize(glm::cross(axis, helper)) * radius;
	const glm::dvec3 v = glm::cross(axis, u);
	glm::dvec3 previous = center + u;
	for (int segment = 1; segment <= segments; ++segment)
	{
		const double angle = 2.0 * std::numbers::pi * segment / segments;
		const glm::dvec3 next = center + u * std::cos(angle) + v * std::sin(angle);
		Line(previous, next, color, depth);
		previous = next;
	}
}

void DebugDraw::Sphere(
    const glm::dvec3 &center, double radius, const glm::vec4 &color, int segments, DebugDepth depth)
{
	Circle(center, {1.0, 0.0, 0.0}, radius, color, segments, depth);
	Circle(center, {0.0, 1.0, 0.0}, radius, color, segments, depth);
	Circle(center, {0.0, 0.0, 1.0}, radius, color, segments, depth);
}

void DebugDraw::Box(
    const glm::dvec3 &center, const glm::dvec3 &halfExtents, const glm::dquat &orientation,
    const glm::vec4 &color, DebugDepth depth)
{
	std::array<glm::dvec3, 8> corners;
	for (int corner = 0; corner < 8; ++corner)
	{
		const glm::dvec3 sign{
		    corner & 1 ? 1.0 : -1.0, corner & 2 ? 1.0 : -1.0, corner & 4 ? 1.0 : -1.0};
		corners[static_cast<std::size_t>(corner)] = center + orientation * (sign * halfExtents);
	}
	// Corners that differ in exactly one bit share an edge.
	for (int corner = 0; corner < 8; ++corner)
		for (const int bit : {1, 2, 4})
			if ((corner & bit) == 0)
				Line(
				    corners[static_cast<std::size_t>(corner)],
				    corners[static_cast<std::size_t>(corner | bit)], color, depth);
}

void DebugDraw::Axes(
    const glm::dvec3 &origin, const glm::dquat &orientation, double length, DebugDepth depth)
{
	Line(origin, origin + orientation * glm::dvec3{length, 0.0, 0.0}, {1.0f, 0.1f, 0.1f, 1.0f}, depth);
	Line(origin, origin + orientation * glm::dvec3{0.0, length, 0.0}, {0.1f, 1.0f, 0.1f, 1.0f}, depth);
	Line(origin, origin + orientation * glm::dvec3{0.0, 0.0, length}, {0.2f, 0.4f, 1.0f, 1.0f}, depth);
}

void DebugDraw::Clear()
{
	m_tested.clear();
	m_overlay.clear();
}

std::uint32_t DebugDraw::Build(const glm::dvec3 &camera, std::vector<LineVertex> &vertices) const
{
	vertices.clear();
	vertices.reserve(2 * (m_tested.size() + m_overlay.size()));
	for (const auto *list : {&m_tested, &m_overlay})
		for (const Segment &segment : *list)
		{
			vertices.push_back({ToRenderSpace(segment.from, camera), segment.color});
			vertices.push_back({ToRenderSpace(segment.to, camera), segment.color});
		}
	return static_cast<std::uint32_t>(2 * m_tested.size());
}
} // namespace Jettison::Render
