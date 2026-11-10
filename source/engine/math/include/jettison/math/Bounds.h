#pragma once

#include <glm/glm.hpp>

#include <array>

namespace Jettison::Math
{
// Bounding volumes, in the frame of whoever holds them. Each shape comes in float, for render-side
// culling and picking in camera-relative space, and in double, for the simulation's system frame:
// there positions are metres from the star, and a float is already a quarter of a metre coarse
// at four thousand kilometres. Plain data; the tests in Intersect.h are the behaviour, compiled
// once for each precision in Intersect.cpp.
template <typename T> struct BasicAabb
{
	glm::vec<3, T> min{T(0)};
	glm::vec<3, T> max{T(0)};
};

template <typename T> struct BasicSphere
{
	glm::vec<3, T> centre{T(0)};
	T radius{T(0)};
};

// Unit normal and signed distance from the origin: dot(normal, p) + distance == 0 on the plane,
// and positive in front of it.
template <typename T> struct BasicPlane
{
	glm::vec<3, T> normal{T(0), T(1), T(0)};
	T distance{T(0)};
};

// The direction need not be unit length: a hit's distance is measured in multiples of it.
template <typename T> struct BasicRay
{
	glm::vec<3, T> origin{T(0)};
	glm::vec<3, T> direction{T(0), T(0), T(-1)};
};

// Centre, half extents along the box's own axes, and the rotation taking those axes to the frame.
template <typename T> struct BasicObb
{
	glm::vec<3, T> centre{T(0)};
	glm::vec<3, T> halfExtents{T(0)};
	glm::mat<3, 3, T> axes{T(1)};
};

using Aabb = BasicAabb<float>;
using Sphere = BasicSphere<float>;
using Plane = BasicPlane<float>;
using Ray = BasicRay<float>;
using Obb = BasicObb<float>;

using DAabb = BasicAabb<double>;
using DSphere = BasicSphere<double>;
using DPlane = BasicPlane<double>;
using DRay = BasicRay<double>;
using DObb = BasicObb<double>;

// The six planes bounding a clip volume, facing inwards, in clip-space order: x >= -w, x <= w,
// y >= -w, y <= w, then depth 0 and depth 1. The last two are the near and far planes with
// conventional depth, and the far and near planes with reversed-Z (phase 4). Float only: it is
// built from a view-projection matrix, which lives on the render side.
struct Frustum
{
	std::array<Plane, 6> planes{};
};

// The box in a new frame that bounds the box carried by the matrix: the matrix's rotation and
// scale grow it to stay axis-aligned (Arvo's method). This is how a model's local bounds become
// the world bounds culling tests.
Aabb Transformed(const Aabb &box, const glm::mat4 &matrix);
} // namespace Jettison::Math
