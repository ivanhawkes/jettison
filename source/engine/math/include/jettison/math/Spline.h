#pragma once

#include <glm/glm.hpp>

#include <span>

namespace Jettison::Math
{
// Curves for camera rails, route display and docking approaches. Parameter t is in [0, 1] over
// the whole curve. Filled in phase 2.
glm::vec3 CubicBezier(
    const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, float t);

// A Catmull-Rom spline through every point, clamped at the ends.
glm::vec3 CatmullRom(std::span<const glm::vec3> points, float t);
} // namespace Jettison::Math
