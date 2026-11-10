#pragma once

#include <glm/glm.hpp>

#include <optional>

#include <jettison/math/Bounds.h>

namespace Jettison::Math
{
// Intersection tests for culling and picking, defined for float and double (Bounds.h). Touching
// counts as intersecting. A ray test returns how far along the ray the first hit is, in multiples
// of the ray's direction, or nothing; a ray that starts inside a solid hits it at 0.
template <typename T> bool Intersects(const BasicAabb<T> &a, const BasicAabb<T> &b);
template <typename T> bool Intersects(const BasicSphere<T> &a, const BasicSphere<T> &b);
template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicAabb<T> &box);
template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicObb<T> &box);
template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicSphere<T> &sphere);
// A ray parallel to the plane misses it, including one that lies in it.
template <typename T> std::optional<T> Intersect(const BasicRay<T> &ray, const BasicPlane<T> &plane);

// Conservative: a box near a corner of the frustum, outside it but not wholly behind any one
// plane, is reported as intersecting. Culling can afford to draw it; it cannot afford a test
// per box that is exact.
bool Intersects(const Frustum &frustum, const Aabb &box);
bool Intersects(const Frustum &frustum, const Sphere &sphere);

// The frustum of a view-projection matrix with zero-to-one depth (GLM_FORCE_DEPTH_ZERO_TO_ONE),
// planes normalised. A plane at infinity - the far plane of an infinite projection, reversed-Z
// included - has no normal to normalise, and comes back as a zero normal with distance 1, which
// every point is in front of: the tests above need no case for it.
Frustum FrustumFromMatrix(const glm::mat4 &viewProjection);
} // namespace Jettison::Math
