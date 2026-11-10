#include <jettison/render/SceneSystems.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <jettison/animation/Playback.h>
#include <jettison/render/Scene.h>

namespace Jettison::Render
{
using Animation::AnimationPlayerComponent;
using Animation::RotationAnimationComponent;
using Animation::SkeletonComponent;

namespace
{
glm::mat4 ComputeWorldTransform(
    entt::entity entity, entt::registry &registry,
    std::unordered_map<entt::entity, glm::mat4> &computed, std::unordered_set<entt::entity> &active)
{
	if (const auto found = computed.find(entity); found != computed.end())
		return found->second;
	if (!active.insert(entity).second)
		throw std::runtime_error("Scene hierarchy contains a parent cycle");

	const auto &transform = registry.get<TransformComponent>(entity);
	glm::mat4 local{1.0f};
	local = glm::translate(local, transform.translation);
	// The rotation animation's phase joins the rotation here, and is not accumulated in
	// TransformComponent: the authored rotation stays authored, so a scene saved from a running
	// engine writes the pose its author set, not the frame the animation happened to be on.
	glm::vec3 rotation = transform.rotation;
	if (const auto *animation = registry.try_get<RotationAnimationComponent>(entity))
		rotation += animation->rotationAxis * animation->angle;
	local = glm::rotate(local, rotation.x, {1.0f, 0.0f, 0.0f});
	local = glm::rotate(local, rotation.y, {0.0f, 1.0f, 0.0f});
	local = glm::rotate(local, rotation.z, {0.0f, 0.0f, 1.0f});
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
} // namespace

void SceneUpdateSystem::Update(entt::registry &registry, float deltaTime) const
{
	const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
	for (const entt::entity entity : registry.view<TransformComponent, RotationAnimationComponent>())
		Animation::AdvanceRotation(registry.get<RotationAnimationComponent>(entity), frameTime);
	for (const entt::entity entity : registry.view<MeshComponent, SkeletonComponent>())
	{
		const Animation::Rig &rig = registry.get<MeshComponent>(entity).model->GetRig();
		auto &skeleton = registry.get<SkeletonComponent>(entity);
		std::string_view clipName;
		float animationTime = 0.0f;
		if (auto *player = registry.try_get<AnimationPlayerComponent>(entity))
		{
			Animation::AdvancePlayer(*player, rig.AnimationDuration(player->clipName), frameTime);
			clipName = player->clipName;
			animationTime = player->time;
		}
		rig.EvaluateSkin(clipName, animationTime, skeleton.jointMatrices, skeleton.meshNodeTransform);
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
		{
			const auto *skeleton = registry.try_get<SkeletonComponent>(entity);
			const glm::mat4 meshNodeTransform =
			    skeleton ? skeleton->meshNodeTransform : glm::mat4{1.0f};
			drawCommands.push_back(
			    {mesh.model.get(),
			     meshes.get<const WorldTransformComponent>(entity).matrix * meshNodeTransform,
			     skeleton});
		}
	}
	return drawCommands;
}
} // namespace Jettison::Render