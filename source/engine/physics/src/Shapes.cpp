#include "Shapes.h"

#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <string>

#include "Convert.h"

namespace Jettison::Physics::Detail
{
namespace
{
// The farthest a shape's point or size reaches, in metres. A shape is a model's, about its body, and
// in float a coordinate past this is held to a metre, which no shape a body collides as is; within it
// Jolt's convex builder, which takes a face's area squared, a coordinate to the fourth power, stays
// far inside a float. A point at 1.8e31 m from a fuzzed collision file overflowed it, and Jolt asserted
// (normal_len > 0, ConvexHullBuilder::DetermineMaxError) where it should have refused (2026-11-28).
constexpr float kMaxCoordinate = 1.0e7f;


[[noreturn]] void Refuse(std::string_view name, std::string_view problem)
{
	throw std::invalid_argument{std::format("Physics: shape '{}' {}", name, problem)};
}


void RequireSize(std::string_view name, std::string_view what, float value)
{
	if (!std::isfinite(value) || value <= 0.0f || value > kMaxCoordinate)
		Refuse(name, std::format("has a {} of {}, which is not a size", what, value));
}


void RequirePoint(std::string_view name, const glm::vec3 &point)
{
	if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
		Refuse(name, "has a point that is not finite");
	if (std::max({std::abs(point.x), std::abs(point.y), std::abs(point.z)}) > kMaxCoordinate)
		Refuse(name, std::format("has a point farther than {} m from its centre", kMaxCoordinate));
}


JPH::RefConst<JPH::Shape> Create(std::string_view name, const JPH::ShapeSettings &settings)
{
	const JPH::ShapeSettings::ShapeResult result = settings.Create();
	if (result.HasError())
		Refuse(name, std::format("is refused by Jolt: {}", result.GetError().c_str()));
	return result.Get();
}


// The shape's turn as Jolt takes one, of unit length.
JPH::Quat Turn(std::string_view name, const ShapeDefinition &shape)
{
	const glm::quat &turn = shape.turn;
	const float length = glm::length(turn);
	if (!std::isfinite(length) || length <= 0.0f)
		Refuse(name, "has a turn that is not a rotation");
	return JPH::Quat{turn.x, turn.y, turn.z, turn.w}.Normalized();
}


// The shape about its own centre, before its offset and turn place it.
JPH::RefConst<JPH::Shape> BuildCentred(std::string_view name, const ShapeDefinition &shape)
{
	switch (shape.kind)
	{
		case ShapeKind::Box:
		{
			RequireSize(name, "half extent", shape.halfExtents.x);
			RequireSize(name, "half extent", shape.halfExtents.y);
			RequireSize(name, "half extent", shape.halfExtents.z);
			const float smallest =
			    std::min({shape.halfExtents.x, shape.halfExtents.y, shape.halfExtents.z});
			return Create(
			    name, JPH::BoxShapeSettings{
			              ToJolt(shape.halfExtents), std::min(JPH::cDefaultConvexRadius, smallest)});
		}
		case ShapeKind::Sphere:
			RequireSize(name, "radius", shape.radius);
			return Create(name, JPH::SphereShapeSettings{shape.radius});
		case ShapeKind::Capsule:
			RequireSize(name, "radius", shape.radius);
			RequireSize(name, "half height", shape.halfHeight);
			return Create(name, JPH::CapsuleShapeSettings{shape.halfHeight, shape.radius});
		case ShapeKind::Cylinder:
			RequireSize(name, "radius", shape.radius);
			RequireSize(name, "half height", shape.halfHeight);
			return Create(
			    name, JPH::CylinderShapeSettings{
			              shape.halfHeight, shape.radius,
			              std::min({JPH::cDefaultConvexRadius, shape.halfHeight, shape.radius})});
		case ShapeKind::ConvexHull:
		{
			if (shape.points.size() < 4)
				Refuse(
				    name,
				    std::format(
				        "is a convex hull of {} points, and needs at least 4", shape.points.size()));
			JPH::Array<JPH::Vec3> points;
			points.reserve(shape.points.size());
			for (const glm::vec3 &point : shape.points)
			{
				RequirePoint(name, point);
				points.push_back(ToJolt(point));
			}
			return Create(name, JPH::ConvexHullShapeSettings{points});
		}
		case ShapeKind::Mesh:
		{
			if (shape.indices.empty() || shape.indices.size() % 3 != 0)
				Refuse(
				    name,
				    std::format("has {} indices, which is not whole triangles", shape.indices.size()));
			JPH::VertexList vertices;
			vertices.reserve(shape.vertices.size());
			for (const glm::vec3 &vertex : shape.vertices)
			{
				RequirePoint(name, vertex);
				vertices.push_back({vertex.x, vertex.y, vertex.z});
			}
			JPH::IndexedTriangleList triangles;
			triangles.reserve(shape.indices.size() / 3);
			for (std::size_t index = 0; index < shape.indices.size(); index += 3)
			{
				const std::uint32_t a = shape.indices[index];
				const std::uint32_t b = shape.indices[index + 1];
				const std::uint32_t c = shape.indices[index + 2];
				if (std::max({a, b, c}) >= vertices.size())
					Refuse(name, std::format("has an index past its {} vertices", vertices.size()));
				triangles.push_back({a, b, c});
			}
			return Create(name, JPH::MeshShapeSettings{std::move(vertices), std::move(triangles)});
		}
		case ShapeKind::Compound:
		{
			if (shape.parts.empty())
				Refuse(name, "is a compound of no parts");

			// Static: a shape is a definition, never changed once built, and Jolt's static compound
			// is the one with a tree over its parts.
			JPH::StaticCompoundShapeSettings compound;
			for (std::size_t index = 0; index < shape.parts.size(); ++index)
			{
				const ShapeDefinition &part = shape.parts[index];
				const std::string partName = std::format("{}, part {}", name, index);
				RequirePoint(partName, part.offset);
				compound.AddShape(
				    ToJolt(part.offset), Turn(partName, part), BuildCentred(partName, part));
			}
			return Create(name, compound);
		}
	}
	Refuse(name, "is of no kind physics knows");
}
} // namespace


JPH::RefConst<JPH::Shape> BuildShape(std::string_view name, const ShapeDefinition &shape)
{
	RequirePoint(name, shape.offset);
	const JPH::Quat turn = Turn(name, shape);
	JPH::RefConst<JPH::Shape> centred = BuildCentred(name, shape);

	// Left as it is when it is not moved, so a shape with no offset is built as it always was.
	if (shape.offset == glm::vec3{0.0f} && shape.turn == glm::quat{1.0f, 0.0f, 0.0f, 0.0f})
		return centred;
	return Create(name, JPH::RotatedTranslatedShapeSettings{ToJolt(shape.offset), turn, centred});
}


std::uint32_t PartOf(const JPH::Shape &shape, const JPH::SubShapeID &id)
{
	// An offset is Jolt's decoration, which passes a sub-shape's identifier through as it is.
	const JPH::Shape *pShape = &shape;
	if (pShape->GetSubType() == JPH::EShapeSubType::RotatedTranslated)
		pShape = JPH::StaticCast<JPH::DecoratedShape>(pShape)->GetInnerShape();
	if (pShape->GetSubType() != JPH::EShapeSubType::StaticCompound)
		return 0;
	JPH::SubShapeID remainder;
	return JPH::StaticCast<JPH::CompoundShape>(pShape)->GetSubShapeIndexFromID(id, remainder);
}


std::optional<ShapeHit> CastRayOnShape(
    const JPH::Shape &shape, const glm::vec3 &origin, const glm::vec3 &reach)
{
	const JPH::RayCast ray{ToJolt(origin) - shape.GetCenterOfMass(), ToJolt(reach)};
	JPH::RayCastResult hit;
	if (!shape.CastRay(ray, JPH::SubShapeIDCreator{}, hit))
		return std::nullopt;
	return ShapeHit{
	    hit.mFraction,
	    ToGlmFloat(shape.GetSurfaceNormal(hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction))),
	    PartOf(shape, hit.mSubShapeID2)};
}
} // namespace Jettison::Physics::Detail
