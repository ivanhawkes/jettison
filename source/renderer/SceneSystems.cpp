#include "SceneSystems.h"
#include "Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Jettison::Renderer
{
namespace
{
// A turn in radians. The rotation animation's phase is kept inside one: the rotation is
// periodic, so the wrap is not visible, and an hour of running costs the precision of the
// first minute.
constexpr float kFullTurn = 6.28318531f;

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
	{
		auto &animation = registry.get<RotationAnimationComponent>(entity);
		animation.angle = std::fmod(animation.angle + animation.rotationSpeed * frameTime, kFullTurn);
	}
	for (const entt::entity entity : registry.view<MeshComponent, SkeletonComponent>())
	{
		const auto &mesh = registry.get<MeshComponent>(entity);
		auto &skeleton = registry.get<SkeletonComponent>(entity);
		const auto *player = registry.try_get<AnimationPlayerComponent>(entity);
		std::string_view clipName;
		float animationTime = 0.0f;
		if (player)
		{
			clipName = player->clipName;
			animationTime = player->time;
			const float duration = mesh.model->AnimationDuration(clipName);
			if (player->playing && duration > 0.0f)
			{
				auto &mutablePlayer = registry.get<AnimationPlayerComponent>(entity);
				mutablePlayer.time += frameTime * mutablePlayer.playbackSpeed;
				if (mutablePlayer.looping)
					mutablePlayer.time = std::fmod(std::max(mutablePlayer.time, 0.0f), duration);
				else
					mutablePlayer.time = std::clamp(mutablePlayer.time, 0.0f, duration);
				animationTime = mutablePlayer.time;
			}
		}
		mesh.model->EvaluateSkin(
		    clipName, animationTime, skeleton.jointMatrices, skeleton.meshNodeTransform);
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
} // namespace Jettison::Renderer