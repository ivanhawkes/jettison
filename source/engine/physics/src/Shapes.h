#pragma once

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include <string_view>

#include <jettison/physics/Shape.h>

namespace Jettison::Physics::Detail
{
// The Jolt shape a definition describes. Throws std::invalid_argument naming the shape for one
// that does not describe a shape: a mesh whose indices are not whole triangles or name vertices it
// does not have, sizes that are not positive and finite, or anything Jolt itself refuses.
//
// A box's and a cylinder's rounded edges (Jolt's convex radius, which makes contacts cheaper and
// steadier) are Jolt's default 5 cm, less on a shape too small for that, so a small shape is still
// the size it says.
JPH::RefConst<JPH::Shape> BuildShape(std::string_view name, const ShapeDefinition &shape);
} // namespace Jettison::Physics::Detail
