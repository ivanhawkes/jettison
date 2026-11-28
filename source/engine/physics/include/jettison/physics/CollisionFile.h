#pragma once

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <jettison/physics/Shape.h>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Physics
{
// A model's collision shape in a file of its own (docs/physics.md, "A model's collision shape"),
// which a tool bakes from the model and the simulation reads, so a server collides as a client does
// without reading the model: a Compound whose parts are each a ConvexHull or a Mesh, in the model's
// axes and metres. Written by Serialise::BinaryArchive as a "Physics.Shape" of kCollisionFileVersion,
// so the same shape is the same bytes. World content (Host::kWorldDirectories), named by the data
// that uses it: a model's is by convention at its path with "collision/" for "models/" and
// ".collision" for its extension.
inline constexpr std::uint32_t kCollisionFileVersion = 1;

// Throws Serialise::FormatError, naming the file, for a shape that is not a collision file's: not a
// compound, or one of no parts, or with a part that is not a convex hull or a mesh. The writer and
// the readers go through it. Jolt's own refusals (too few points, indices past the vertices) are
// DefineShape's, as for any shape.
void CheckCollisionShape(std::string_view path, const ShapeDefinition &shape);

// The file's bytes. Throws as CheckCollisionShape does.
std::vector<std::byte> WriteCollisionFile(const ShapeDefinition &shape, std::string_view path);

// The shape a file holds. Throws Serialise::FormatError naming the path for bytes that are not one,
// of another version among them, and as CheckCollisionShape does.
ShapeDefinition ReadCollisionFile(std::span<const std::byte> bytes, std::string_view path);

// The same, read through the file system. Throws std::runtime_error for a path it holds no file at.
ShapeDefinition ReadCollisionFile(const Vfs::FileSystem &files, std::string_view path);

// The points of the convex hull of those given that Jolt's builder keeps, at most maxPoints of them,
// in the order they were given: the points themselves, so a convex hull of many points is
// simplified to the ones that shape it most, with none moved. Points on a plane make a flat convex
// hull of their outline. Throws std::invalid_argument for fewer than 4 points, a bound under 4, or
// points that span no area (all on a line).
std::vector<glm::vec3> ConvexHullPoints(std::span<const glm::vec3> points, std::size_t maxPoints);
} // namespace Jettison::Physics
