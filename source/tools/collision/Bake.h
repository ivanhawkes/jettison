#pragma once

#include <cstddef>
#include <filesystem>
#include <string_view>

#include <jettison/physics/CollisionFile.h>
#include <jettison/physics/Shape.h>

namespace Jettison::Bake
{
// A node a model's author made its collision shape of, never drawn (the art standards' role nodes):
// "collision-<name>" is a convex hull of its vertices, and "collision-mesh-<name>" a mesh of its
// triangles, which only a static or kinematic body can collide as.
inline constexpr std::string_view kCollisionPrefix = "collision-";
inline constexpr std::string_view kCollisionMeshPrefix = "collision-mesh-";

// The most points a convex hull simplified from a drawn mesh keeps: a contact with a convex hull costs
// by its points, so a coarse outline, a best guess not yet measured against a real model; an author
// wanting a closer one makes collision- nodes.
inline constexpr std::size_t kDrawnConvexHullPoints = 32;

// The most an author's convex hull keeps: Jolt's own limit (ConvexHullShape::cMaxPointsInHull), so
// what the author made is kept whole within it.
inline constexpr std::size_t kAuthoredConvexHullPoints = 256;

// A model's collision shape (a glTF or GLB file), for Physics::WriteCollisionFile: a compound in the
// model's axes and metres, its parts the mesh nodes of its scene in the scene's order, each moved by
// its node's transform. Its collision- nodes where it has any, the rest left out; else a convex hull
// of each mesh node, simplified to kDrawnConvexHullPoints. The same model gives the same shape.
// Throws std::runtime_error naming the model for one that does not read, has no scene or no mesh
// node, has a primitive that is not triangles or has no positions, or a node whose points make no
// convex hull (all on a line).
Physics::ShapeDefinition CollisionShape(const std::filesystem::path &model);
} // namespace Jettison::Bake
