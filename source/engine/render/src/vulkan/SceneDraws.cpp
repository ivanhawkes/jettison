#include "vulkan/SceneDraws.h"

#include <utility>

#include <jettison/render/Camera.h>
#include <jettison/render/PostProcess.h>
#include <jettison/scene/Components.h>

#include "vulkan/Model.h"
#include "vulkan/ModelCache.h"

namespace Jettison::Render
{
namespace
{
// The nearest of an entity and its ancestors to have a component, and the component, or null.
template <typename Component>
std::pair<entt::entity, const Component *> Nearest(const entt::registry &registry, entt::entity entity)
{
	// A parent chain is a few links; the bound only stops a cycle a broken file could make.
	for (int depth = 0; depth < 64 && registry.valid(entity); ++depth)
	{
		if (const auto *component = registry.try_get<Component>(entity))
			return {entity, component};
		const auto *parent = registry.try_get<Scene::ParentComponent>(entity);
		if (parent == nullptr)
			break;
		entity = parent->entity;
	}
	return {entt::null, nullptr};
}
} // namespace

std::vector<SceneDrawCommand> CollectDraws(
    const entt::registry &registry, const glm::dvec3 &camera, ModelCache &models)
{
	using Scene::MeshComponent;
	using Scene::Shape;
	std::vector<SceneDrawCommand> drawCommands;
	// A hidden layer's meshes are neither drawn nor cast shadows.
	const auto meshes = registry.view<const MeshComponent, const Scene::WorldTransformComponent>(
	    entt::exclude<Scene::HiddenComponent>);
	for (const entt::entity entity : meshes)
	{
		const auto &mesh = meshes.get<const MeshComponent>(entity);
		// Resolved first, so a model's file is asked for while its rig is still on the way.
		const Model *model = models.Resolve(mesh);
		const auto *skeleton = registry.try_get<Animation::SkeletonComponent>(entity);
		if (model == nullptr || (!mesh.shape && skeleton == nullptr))
			continue;
		const glm::dmat4 meshNodeTransform{skeleton ? skeleton->meshNodeTransform : glm::mat4{1.0f}};
		const glm::dmat4 world =
		    meshes.get<const Scene::WorldTransformComponent>(entity).matrix * meshNodeTransform;
		// The bounds come from the double-precision matrix, made camera-relative before anything is
		// rounded: a planet's centre ten thousand kilometres off keeps its metres.
		glm::dmat4 relative = world;
		relative[3] = glm::dvec4{glm::dvec3{world[3]} - camera, 1.0};
		// The draw's own transform is ToRenderSpace's, which the large-world test measures.
		SceneDrawCommand draw{
		    model,
		    ToRenderSpace(world, camera),
		    skeleton,
		    &mesh.material,
		    TransformBounds(model->m_bounds, relative),
		    MaxScale(relative),
		    double{model->m_bounds.radius} * MiddleScale(relative),
		    0.0};
		const bool round = mesh.shape == Shape::Sphere || mesh.shape == Shape::IcoSphere ||
		                   mesh.shape == Shape::CubeSphere;
		if (round && registry.all_of<Scene::CelestialBodyComponent>(entity))
		{
			draw.occluderRadius = MinScale(relative);
			draw.atmosphere = registry.try_get<Scene::AtmosphereComponent>(entity);
			draw.rings = registry.try_get<Scene::RingsComponent>(entity);
			draw.spinAxis = glm::vec3{glm::normalize(glm::dvec3{world[1]})};
			draw.color = mesh.color;
		}
		const auto [owner, outline] = Nearest<Scene::OutlineComponent>(registry, entity);
		draw.outline = outline;
		draw.outlineOwner = owner;
		draw.stencil =
		    StencilBits(Nearest<Scene::MaskComponent>(registry, entity).second, outline != nullptr);
		drawCommands.push_back(draw);
	}
	return drawCommands;
}
} // namespace Jettison::Render
