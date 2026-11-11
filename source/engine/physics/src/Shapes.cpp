#include "Shapes.h"

#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

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
[[noreturn]] void Refuse(std::string_view name, std::string_view problem)
{
	throw std::invalid_argument{std::format("Physics: shape '{}' {}", name, problem)};
}

void RequireSize(std::string_view name, std::string_view what, float value)
{
	if (!std::isfinite(value) || value <= 0.0f)
		Refuse(name, std::format("has a {} of {}, which is not a size", what, value));
}

void RequirePoint(std::string_view name, const glm::vec3 &point)
{
	if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
		Refuse(name, "has a point that is not finite");
}

JPH::RefConst<JPH::Shape> Create(std::string_view name, const JPH::ShapeSettings &settings)
{
	const JPH::ShapeSettings::ShapeResult result = settings.Create();
	if (result.HasError())
		Refuse(name, std::format("is refused by Jolt: {}", result.GetError().c_str()));
	return result.Get();
}
} // namespace

JPH::RefConst<JPH::Shape> BuildShape(std::string_view name, const ShapeDefinition &shape)
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
				    std::format("is a hull of {} points, and needs at least 4", shape.points.size()));
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
	}
	Refuse(name, "is of no kind physics knows");
}
} // namespace Jettison::Physics::Detail
