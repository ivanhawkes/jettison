#include <jettison/physics/SnapshotPicker.h>

#include <jettison/physics/Components.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Snapshot.h>

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Physics/Collision/Shape/Shape.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <format>
#include <map>
#include <stdexcept>
#include <string_view>

#include "Convert.h"
#include "FrameWorld.h"
#include "JoltRuntime.h"
#include "Shapes.h"

namespace Jettison::Physics
{
namespace
{
// Jolt's drawing of a shape, each line handed on as a segment of the body being drawn. Jolt's
// colours and text are dropped: the caller colours a body as it chooses.
class LineRenderer final : public JPH::DebugRendererSimple
{
  public:
	void Begin(entt::entity entity, const Body &body, const BodyLine &line)
	{
		m_entity = entity;
		m_pBody = &body;
		m_pLine = &line;
	}


	void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg /*color*/) override
	{
		(*m_pLine)(m_entity, *m_pBody, Detail::ToGlm(from), Detail::ToGlm(to));
	}


	void DrawText3D(
	    JPH::RVec3Arg /*position*/, const std::string_view & /*text*/, JPH::ColorArg /*color*/,
	    float /*height*/) override
	{
	}

  private:
	entt::entity m_entity{entt::null};
	const Body *m_pBody{nullptr};
	const BodyLine *m_pLine{nullptr};
};
} // namespace


struct SnapshotPicker::Impl
{
	const PhysicsWorld *pWorld{nullptr};

	// The picker's own, for one with no world.
	LayerDefinitions layers;

	// Built once a name, from a definition given here or the world's; mutable, as a cache.
	mutable std::map<std::string, JPH::RefConst<JPH::Shape>, std::less<>> shapes;

	// Made by the first draw, since Jolt builds its round shapes' geometry as it is made.
	mutable std::unique_ptr<LineRenderer> pRenderer;


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


	const LayerDefinitions &Layers() const
	{
		return pWorld ? pWorld->Layers() : layers;
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


void SnapshotPicker::DefineLayers(const LayerDefinitions &layers)
{
	if (m_pImpl->pWorld != nullptr)
		throw std::logic_error{"Physics: a picker with a physics world has the world's layers"};
	if (!m_pImpl->layers.empty())
		throw std::logic_error{"Physics: the picker's layers are already defined"};
	Detail::CheckLayers(layers);
	m_pImpl->layers = layers;
}


void SnapshotPicker::DefineLayers(const Sim::DefinitionRegistry &definitions)
{
	LayerDefinitions layers;
	for (const std::string &name : definitions.Names<LayerDefinition>())
		layers.emplace(name, *definitions.Find<LayerDefinition>(name));
	DefineLayers(layers);
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
	const LayerDefinitions &layers = m_pImpl->Layers();
	if (options.layer && !layers.contains(*options.layer))
		throw std::invalid_argument{
		    std::format("Physics: a pick as layer '{}', which is not defined", *options.layer)};

	std::optional<CastHit> nearest;
	for (const auto &[entity, body] : current.Components<Body>())
	{
		if (entity == options.ignore || (body.sensor && !options.sensors) ||
		    (body.kind == BodyKind::Static && !options.statics) ||
		    !LayersMeet(layers, options.layer, body.layer))
			continue;
		const Sim::TransformSample *pSample = current.FindSample(entity);
		if (pSample == nullptr || pSample->frame != frame)
			continue;
		const JPH::Shape *pShape = m_pImpl->Find(body.shape);
		const std::optional<Sim::Transform> place = Sim::Interpolate(previous, current, entity, alpha);
		if (pShape == nullptr || !place)
			continue;

		// In the body's own frame: metres from it, so floats hold them.
		const glm::dquat inverse = glm::inverse(place->orientation);
		const std::optional<Detail::ShapeHit> hit = Detail::CastRayOnShape(
		    *pShape, glm::vec3{inverse * (origin - place->position)}, glm::vec3{inverse * reach});
		if (!hit)
			continue;
		const double distance = static_cast<double>(hit->fraction) * maxDistance;

		// The bodies come in entity order, so a tie is the lower entity's.
		if (nearest && nearest->distance <= distance)
			continue;
		const glm::vec3 normal = glm::vec3{place->orientation * glm::dvec3{hit->normal}};
		nearest = CastHit{
		    entity, distance, origin + reach * static_cast<double>(hit->fraction),
		    glm::normalize(normal), hit->part};
	}
	return nearest;
}


void SnapshotPicker::DrawBodies(
    const Sim::Snapshot &previous, const Sim::Snapshot &current, double alpha, std::uint64_t frame,
    const glm::dvec3 &camera, const BodyLine &line) const
{
	if (!m_pImpl->pRenderer)
		m_pImpl->pRenderer = std::make_unique<LineRenderer>();
	LineRenderer &renderer = *m_pImpl->pRenderer;
	renderer.SetCameraPos(Detail::ToJolt(camera));
	for (const auto &[entity, body] : current.Components<Body>())
	{
		const Sim::TransformSample *pSample = current.FindSample(entity);
		if (pSample == nullptr || pSample->frame != frame)
			continue;
		const JPH::Shape *pShape = m_pImpl->Find(body.shape);
		const std::optional<Sim::Transform> place = Sim::Interpolate(previous, current, entity, alpha);
		if (pShape == nullptr || !place)
			continue;

		// Jolt draws a shape from its centre of mass, as it casts against one.
		const JPH::Quat rotation = Detail::ToJolt(place->orientation);
		const JPH::RMat44 centre = JPH::RMat44::sRotationTranslation(
		    rotation, Detail::ToJolt(place->position) + rotation * pShape->GetCenterOfMass());
		renderer.Begin(entity, body, line);
		pShape->Draw(&renderer, centre, JPH::Vec3::sOne(), JPH::Color::sWhite, false, true);
	}
}
} // namespace Jettison::Physics
