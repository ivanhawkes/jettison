#include <jettison/physics/SnapshotPicker.h>

#include <jettison/physics/Components.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Snapshot.h>

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <format>
#include <map>
#include <stdexcept>
#include <string_view>

#include "Convert.h"
#include "JoltRuntime.h"
#include "Shapes.h"

namespace Jettison::Physics
{
struct SnapshotPicker::Impl
{
	const PhysicsWorld *pWorld{nullptr};

	// Built once a name, from a definition given here or the world's; mutable, as a cache.
	mutable std::map<std::string, JPH::RefConst<JPH::Shape>, std::less<>> shapes;


	const JPH::Shape *Find(std::string_view name) const
	{
		if (const auto found = shapes.find(name); found != shapes.end())
			return found->second.GetPtr();
		const ShapeDefinition *pDefinition = pWorld ? pWorld->FindShapeDefinition(name) : nullptr;
		if (pDefinition == nullptr)
			return nullptr;
		return shapes.emplace(std::string{name}, Detail::BuildShape(name, *pDefinition))
		    .first->second.GetPtr();
	}
};


SnapshotPicker::SnapshotPicker() : m_pImpl{std::make_unique<Impl>()}
{
	Detail::EnsureJolt();
}


SnapshotPicker::SnapshotPicker(const PhysicsWorld &shapes) : SnapshotPicker()
{
	m_pImpl->pWorld = &shapes;
}


SnapshotPicker::~SnapshotPicker() = default;


void SnapshotPicker::DefineShape(std::string name, const ShapeDefinition &shape)
{
	if (name.empty())
		throw std::invalid_argument{"Physics: a shape needs a name"};
	if (m_pImpl->shapes.contains(name))
		throw std::invalid_argument{std::format("Physics: shape '{}' is already defined", name)};
	JPH::RefConst<JPH::Shape> built = Detail::BuildShape(name, shape);
	m_pImpl->shapes.emplace(std::move(name), std::move(built));
}


void SnapshotPicker::DefineShapes(const Sim::DefinitionRegistry &definitions)
{
	for (const std::string &name : definitions.Names<ShapeDefinition>())
		DefineShape(name, *definitions.Find<ShapeDefinition>(name));
}


std::optional<CastHit> SnapshotPicker::CastRay(
    const Sim::Snapshot &previous, const Sim::Snapshot &current, double alpha, std::uint64_t frame,
    const glm::dvec3 &origin, const glm::dvec3 &direction, double maxDistance,
    const PickOptions &options) const
{
	const double length = glm::length(direction);
	if (!std::isfinite(length) || length <= 0.0 || !std::isfinite(maxDistance) || maxDistance <= 0.0)
		return std::nullopt;
	const glm::dvec3 reach = direction / length * maxDistance;

	std::optional<CastHit> nearest;
	for (const auto &[entity, body] : current.Components<Body>())
	{
		if (entity == options.ignore || (body.sensor && !options.sensors) ||
		    (body.kind == BodyKind::Static && !options.statics))
			continue;
		const Sim::TransformSample *pSample = current.FindSample(entity);
		if (pSample == nullptr || pSample->frame != frame)
			continue;
		const JPH::Shape *pShape = m_pImpl->Find(body.shape);
		const std::optional<Sim::Transform> place = Sim::Interpolate(previous, current, entity, alpha);
		if (pShape == nullptr || !place)
			continue;

		// In the body's own frame, from its shape's centre of mass, as Jolt casts against a shape:
		// metres from it, so floats hold them.
		const glm::dquat inverse = glm::inverse(place->orientation);
		const JPH::RayCast ray{
		    Detail::ToJolt(glm::vec3{inverse * (origin - place->position)}) - pShape->GetCenterOfMass(),
		    Detail::ToJolt(glm::vec3{inverse * reach})};
		JPH::RayCastResult hit;
		if (!pShape->CastRay(ray, JPH::SubShapeIDCreator{}, hit))
			continue;
		const double distance = static_cast<double>(hit.mFraction) * maxDistance;

		// The bodies come in entity order, so a tie is the lower entity's.
		if (nearest && nearest->distance <= distance)
			continue;
		const JPH::Vec3 local = ray.GetPointOnRay(hit.mFraction);
		const glm::vec3 normal = glm::vec3{
		    place->orientation *
		    glm::dvec3{Detail::ToGlmFloat(pShape->GetSurfaceNormal(hit.mSubShapeID2, local))}};
		nearest = CastHit{
		    entity, distance, origin + reach * static_cast<double>(hit.mFraction),
		    glm::normalize(normal)};
	}
	return nearest;
}
} // namespace Jettison::Physics
