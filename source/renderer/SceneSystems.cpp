#include "SceneSystems.h"
#include "Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Jettison::Renderer
{
namespace
{
glm::mat4 ComputeWorldTransform(
	entt::entity entity, entt::registry &registry,
	std::unordered_map<entt::entity, glm::mat4> &computed,
	std::unordered_set<entt::entity> &active)
{
	if (const auto found = computed.find(entity); found != computed.end())
		return found->second;
	if (!active.insert(entity).second)
		throw std::runtime_error("Scene hierarchy contains a parent cycle");

	const auto &transform = registry.get<TransformComponent>(entity);
	glm::mat4 local{1.0f};
	local = glm::translate(local, transform.translation);
	local = glm::rotate(local, transform.rotation.x, {1.0f, 0.0f, 0.0f});
	local = glm::rotate(local, transform.rotation.y, {0.0f, 1.0f, 0.0f});
	local = glm::rotate(local, transform.rotation.z, {0.0f, 0.0f, 1.0f});
	local = glm::scale(local, transform.scale);

	if (const auto *parent = registry.try_get<ParentComponent>(entity))
	{
		if (!registry.valid(parent->entity))
			throw std::runtime_error("Scene hierarchy contains an invalid parent");
		local = ComputeWorldTransform(parent->entity, registry, computed, active) * local;
	}

	active.erase(entity);
	computed.emplace(entity, local);
	registry.emplace_or_replace<WorldTransformComponent>(entity, local);
	return local;
}
}

void SceneUpdateSystem::Update(entt::registry &registry, float deltaTime) const
{
	const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
	for (const entt::entity entity : registry.view<TransformComponent, RotationAnimationComponent>())
	{
		auto &transform = registry.get<TransformComponent>(entity);
		const auto &animation = registry.get<RotationAnimationComponent>(entity);
		transform.rotation += animation.rotationAxis * animation.rotationSpeed * frameTime;
	}

	std::unordered_map<entt::entity, glm::mat4> computed;
	std::unordered_set<entt::entity> active;
	for (const entt::entity entity : registry.view<TransformComponent>())
		ComputeWorldTransform(entity, registry, computed, active);
}

std::vector<SceneDrawCommand> SceneRenderSystem::Collect(const entt::registry &registry) const
{
	std::vector<SceneDrawCommand> drawCommands;
	const auto meshes = registry.view<const MeshComponent, const WorldTransformComponent>();
	for (const entt::entity entity : meshes)
	{
		const auto &mesh = meshes.get<const MeshComponent>(entity);
		if (mesh.model)
			drawCommands.push_back(
			    {mesh.model.get(), meshes.get<const WorldTransformComponent>(entity).matrix});
	}
	return drawCommands;
}
} // namespace Jettison::Renderer