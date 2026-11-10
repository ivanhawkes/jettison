#pragma once

#include <glm/glm.hpp>

namespace Jettison::Math
{
// Bounding volumes, in the frame of whoever holds them: float for render-side culling and picking,
// with double aliases below for the simulation's system frame. Plain data; the tests in
// Intersect.h are the behaviour. Filled in phase 2.
struct Aabb
{
	glm::vec3 min{0.0f};
	glm::vec3 max{0.0f};
};

struct Sphere
{
	glm::vec3 centre{0.0f};
	float radius{0.0f};
};

// Unit normal and signed distance from the origin: dot(normal, p) + distance == 0 on the plane.
struct Plane
{
	glm::vec3 normal{0.0f, 1.0f, 0.0f};
	float distance{0.0f};
};

struct Ray
{
	glm::vec3 origin{0.0f};
	glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

// Centre, half extents along the box's own axes, and the rotation taking those axes to the frame.
struct Obb
{
	glm::vec3 centre{0.0f};
	glm::vec3 halfExtents{0.0f};
	glm::mat3 axes{1.0f};
};

// Six inward-facing planes: left, right, bottom, top, near, far.
struct Frustum
{
	Plane planes[6];
};

struct DAabb
{
	glm::dvec3 min{0.0};
	glm::dvec3 max{0.0};
};
} // namespace Jettison::Math
