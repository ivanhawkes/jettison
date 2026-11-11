#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <vector>

namespace Jettison::Physics
{
enum class ShapeKind : std::uint8_t
{
	Box,
	Sphere,
	Capsule,
	Cylinder,
	ConvexHull,
	Mesh
};

// A collision shape, in the body's own frame, in metres: a definition the game loads with the
// definition registry (Sim::DefinitionRegistry, from a directory of its choosing) and defines in
// the physics world by name, or defines in code. Bodies name it, so a save names it too and never
// holds its geometry. Only the fields its kind reads are used. Registered with Reflect as
// "Physics.Shape", every field Saved and Moddable, by RegisterTypes.
//
// Floats, as Jolt keeps a shape: a shape is small beside the system it is in, and its body's
// position is what is in double precision.
struct ShapeDefinition
{
	ShapeKind kind{ShapeKind::Box};
	// Box: half of each side.
	glm::vec3 halfExtents{0.5f};
	// Sphere, Capsule and Cylinder.
	float radius{0.5f};
	// Capsule and Cylinder: half the length of the axis, along Y. A capsule's is its cylinder's,
	// without the caps.
	float halfHeight{0.5f};
	// ConvexHull: the points it is the hull of.
	std::vector<glm::vec3> points;
	// Mesh: triangles, three indices each into vertices, counter-clockwise seen from outside. A
	// mesh has no inside, so it is for static and kinematic bodies only.
	std::vector<glm::vec3> vertices;
	std::vector<std::uint32_t> indices;
};
} // namespace Jettison::Physics
