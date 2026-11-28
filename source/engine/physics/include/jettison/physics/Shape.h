#pragma once

#include <glm/gtc/quaternion.hpp>
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
	Mesh,
	Compound
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

	// Where the shape sits in its body, of any kind: its centre moved by the offset and turned by the
	// turn, in the body's axes, so a body's origin need not be its shape's centre (a wheel's axle, a
	// door's hinge). In a compound's part, where the part sits in the compound. A turn of no length
	// or one not finite is refused; any other is normalised.
	glm::vec3 offset{0.0f};
	glm::quat turn{1.0f, 0.0f, 0.0f, 0.0f};

	// Box: half of each side.
	glm::vec3 halfExtents{0.5f};

	// Sphere, Capsule and Cylinder.
	float radius{0.5f};

	// Capsule and Cylinder: half the length of the axis, along Y. A capsule's is its cylinder's,
	// without the caps.
	float halfHeight{0.5f};

	// ConvexHull: the points whose convex envelope it is.
	std::vector<glm::vec3> points;

	// Mesh: triangles, three indices each into vertices, counter-clockwise seen from outside. A
	// mesh has no inside, so it is for static and kinematic bodies only.
	std::vector<glm::vec3> vertices;
	std::vector<std::uint32_t> indices;

	// Compound: the shapes it is made of, each placed by its own offset and turn, at least one. A hit
	// on a compound names the part it hit by its index here (CastHit::part). A compound with a mesh
	// among its parts is a mesh to whatever refuses one.
	std::vector<ShapeDefinition> parts;
};
} // namespace Jettison::Physics
