#include <jettison/physics/CollisionFile.h>

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Geometry/ConvexHullBuilder.h>

#include <format>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>

#include <jettison/physics/Components.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

#include "Convert.h"
#include "JoltRuntime.h"

namespace Jettison::Physics
{
namespace
{
// How far outside the convex hull a point given may be left: the default of Jolt's tolerance for a
// convex hull it builds from points (ConvexHullShapeSettings), a millimetre.
constexpr float kConvexHullTolerance = 1.0e-3f;

// The fewest points a convex hull is built of: a tetrahedron's.
constexpr std::size_t kFewestConvexHullPoints = 4;


[[noreturn]] void Refuse(std::string_view path, std::string_view problem)
{
	throw Serialise::FormatError{std::format("Physics: collision file '{}' {}", path, problem)};
}
} // namespace


void CheckCollisionShape(std::string_view path, const ShapeDefinition &shape)
{
	if (shape.kind != ShapeKind::Compound)
		Refuse(path, "is not a compound");
	if (shape.parts.empty())
		Refuse(path, "is a compound of no parts");
	for (std::size_t index = 0; index < shape.parts.size(); ++index)
	{
		const ShapeKind kind = shape.parts[index].kind;
		if (kind != ShapeKind::ConvexHull && kind != ShapeKind::Mesh)
			Refuse(path, std::format("has a part, {}, that is not a convex hull or a mesh", index));
	}
}


std::vector<std::byte> WriteCollisionFile(const ShapeDefinition &shape, std::string_view path)
{
	RegisterTypes();
	CheckCollisionShape(path, shape);
	return Serialise::BinaryArchive::Write(shape, {kCollisionFileVersion});
}


ShapeDefinition ReadCollisionFile(std::span<const std::byte> bytes, std::string_view path)
{
	RegisterTypes();
	ShapeDefinition shape;
	try
	{
		shape = Serialise::BinaryArchive::Read<ShapeDefinition>(bytes, {kCollisionFileVersion});
	}
	catch (const std::runtime_error &error)
	{
		Refuse(path, std::format("does not read: {}", error.what()));
	}
	CheckCollisionShape(path, shape);
	return shape;
}


ShapeDefinition ReadCollisionFile(const Vfs::FileSystem &files, std::string_view path)
{
	const std::optional<std::vector<std::byte>> bytes = files.Read(path);
	if (!bytes)
		throw std::runtime_error{std::format("Physics: no collision file at '{}'", path)};
	return ReadCollisionFile(*bytes, path);
}


std::vector<glm::vec3> ConvexHullPoints(std::span<const glm::vec3> points, std::size_t maxPoints)
{
	if (points.size() < kFewestConvexHullPoints || maxPoints < kFewestConvexHullPoints)
		throw std::invalid_argument{std::format(
		    "Physics: a convex hull of {} points at most {} is not one: it needs at least {}",
		    points.size(), maxPoints, kFewestConvexHullPoints)};
	Detail::EnsureJolt();
	JPH::ConvexHullBuilder::Positions positions;
	positions.reserve(points.size());
	for (const glm::vec3 &point : points)
		positions.push_back(Detail::ToJolt(point));
	JPH::ConvexHullBuilder builder{positions};
	const char *pError = nullptr;
	const JPH::ConvexHullBuilder::EResult result =
	    builder.Initialize(static_cast<int>(maxPoints), kConvexHullTolerance, pError);
	if (result != JPH::ConvexHullBuilder::EResult::Success &&
	    result != JPH::ConvexHullBuilder::EResult::MaxVerticesReached)
		throw std::invalid_argument{std::format(
		    "Physics: {} points make no convex hull: {}", points.size(), pError ? pError : "")};

	// By index, so the points keep the order they were given in, whatever order the faces are in.
	std::set<int> kept;
	for (const JPH::ConvexHullBuilder::Face *pFace : builder.GetFaces())
	{
		if (pFace->mRemoved)
			continue;
		const JPH::ConvexHullBuilder::Edge *pEdge = pFace->mFirstEdge;
		do
		{
			kept.insert(pEdge->mStartIdx);
			pEdge = pEdge->mNextEdge;
		} while (pEdge != pFace->mFirstEdge);
	}
	std::vector<glm::vec3> shaping;
	shaping.reserve(kept.size());
	for (const int index : kept)
		shaping.push_back(points[static_cast<std::size_t>(index)]);
	return shaping;
}
} // namespace Jettison::Physics
