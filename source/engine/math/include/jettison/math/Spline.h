#pragma once

#include <glm/glm.hpp>

#include <span>

namespace Jettison::Math
{
// Curves for camera rails, route display and docking approaches. Parameter t is clamped to
// [0, 1] over the whole curve.
glm::vec3 CubicBezier(
    const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t);

// A Catmull-Rom spline through every point, each span between two points taking an equal share
// of t. Centripetal rather than uniform: with unevenly spaced points a uniform spline overshoots
// and can loop back on itself between them, and a camera on a rail would follow it. The ends are
// extended by reflecting the second point through the first, so the curve leaves each end
// heading at its neighbour. One point is a constant; there must be at least one.
glm::vec3 CatmullRom(std::span<const glm::vec3> points, float t);
} // namespace Jettison::Math
