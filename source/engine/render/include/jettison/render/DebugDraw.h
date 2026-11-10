#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Jettison::Render
{
// A debug line's vertex as the GPU reads it: render space, and a linear colour, which may go
// above 1, since lines are drawn into the HDR target and exposed with everything else.
struct LineVertex
{
	glm::vec3 position{0.0f};
	glm::vec4 color{1.0f};
};

// Whether a line is hidden by what is in front of it, or drawn over everything.
enum class DebugDepth
{
	Tested,
	Overlay
};

// Lines the frame draws for whoever is debugging it: axes, bounds, orbits, a camera's target. Any
// system adds to it during the frame, in the system frame and in double precision like the
// positions they come from, so a line from a station to its planet is as good as either end; the
// renderer makes them camera-relative when it builds the frame, and the shell clears them when
// the next frame starts. Shapes are made of segments here, so the GPU side is one line list.
class DebugDraw
{
  public:
	void Line(
	    const glm::dvec3 &from, const glm::dvec3 &to, const glm::vec4 &color,
	    DebugDepth depth = DebugDepth::Tested);
	void Polyline(
	    std::span<const glm::dvec3> points, const glm::vec4 &color, bool closed,
	    DebugDepth depth = DebugDepth::Tested);
	// A circle about the normal, through `segments` points.
	void Circle(
	    const glm::dvec3 &center, const glm::dvec3 &normal, double radius, const glm::vec4 &color,
	    int segments = 64, DebugDepth depth = DebugDepth::Tested);
	// Three great circles, one about each axis.
	void Sphere(
	    const glm::dvec3 &center, double radius, const glm::vec4 &color, int segments = 48,
	    DebugDepth depth = DebugDepth::Tested);
	// An oriented box's twelve edges.
	void Box(
	    const glm::dvec3 &center, const glm::dvec3 &halfExtents, const glm::dquat &orientation,
	    const glm::vec4 &color, DebugDepth depth = DebugDepth::Tested);
	// X red, Y green, Z blue, each `length` long.
	void Axes(
	    const glm::dvec3 &origin, const glm::dquat &orientation, double length,
	    DebugDepth depth = DebugDepth::Tested);

	void Clear();
	bool Empty() const
	{
		return m_tested.empty() && m_overlay.empty();
	}
	std::size_t SegmentCount(DebugDepth depth) const
	{
		return depth == DebugDepth::Tested ? m_tested.size() : m_overlay.size();
	}

	// The frame's line list in render space: the depth-tested segments first, then the overlay
	// ones, two vertices each. Returns how many of the vertices are depth-tested.
	std::uint32_t Build(const glm::dvec3 &camera, std::vector<LineVertex> &vertices) const;

  private:
	struct Segment
	{
		glm::dvec3 from;
		glm::dvec3 to;
		glm::vec4 color;
	};

	std::vector<Segment> &List(DebugDepth depth)
	{
		return depth == DebugDepth::Tested ? m_tested : m_overlay;
	}

	std::vector<Segment> m_tested;
	std::vector<Segment> m_overlay;
};
} // namespace Jettison::Render
