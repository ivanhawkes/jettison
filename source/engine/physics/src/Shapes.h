#pragma once

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string_view>

#include <jettison/physics/Shape.h>

namespace Jettison::Physics::Detail
{
// The Jolt shape a definition describes. Throws std::invalid_argument naming the shape for one
// that does not describe a shape: a mesh whose indices are not whole triangles or name vertices it
// does not have, sizes that are not positive and finite, an offset or turn that places nothing, a
// compound of no parts, or anything Jolt itself refuses; a part's problem names the part.
//
// A box's and a cylinder's rounded edges (Jolt's convex radius, which makes contacts cheaper and
// steadier) are Jolt's default 5 cm, less on a shape too small for that, so a small shape is still
// the size it says.
JPH::RefConst<JPH::Shape> BuildShape(std::string_view name, const ShapeDefinition &shape);

// The index in its definition's parts of the part of a compound that a hit's sub-shape is on, for a
// shape BuildShape built; 0 for a shape that is not a compound, or one of a single part, which Jolt
// builds as that part alone.
std::uint32_t PartOf(const JPH::Shape &shape, const JPH::SubShapeID &id);

// What a ray met on a shape: the share of its reach along it, Jolt's surface normal there in the
// shape's axes, and the part (PartOf).
struct ShapeHit
{
	float fraction{0.0f};
	glm::vec3 normal{0.0f};
	std::uint32_t part{0};
};

// The first point of a shape BuildShape built that a ray meets, the ray in the shape's own axes from
// its origin, not from Jolt's centre of mass, which a compound's is moved to. A ray from inside a
// convex part meets it at once; one from inside a mesh passes out through it.
std::optional<ShapeHit> CastRayOnShape(
    const JPH::Shape &shape, const glm::vec3 &origin, const glm::vec3 &reach);
} // namespace Jettison::Physics::Detail
