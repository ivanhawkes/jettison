#pragma once

#include <glm/glm.hpp>

#include <optional>

#include <jettison/math/Bounds.h>

namespace Jettison::Math
{
// Intersection tests for culling and picking. A ray test returns the distance along the ray to
// the first hit, or nothing. Filled in phase 2, with a test per pair against hand-computed cases.
bool Intersects(const Aabb &a, const Aabb &b);
bool Intersects(const Sphere &a, const Sphere &b);
bool Intersects(const Frustum &frustum, const Aabb &box);
bool Intersects(const Frustum &frustum, const Sphere &sphere);
std::optional<float> Intersect(const Ray &ray, const Aabb &box);
std::optional<float> Intersect(const Ray &ray, const Sphere &sphere);
std::optional<float> Intersect(const Ray &ray, const Plane &plane);

// The frustum of a view-projection matrix, planes normalised. Reversed-Z with an infinite far
// plane (phase 4) means the far plane is absent; the function says how it reports that.
Frustum FrustumFromMatrix(const glm::mat4 &viewProjection);
} // namespace Jettison::Math
