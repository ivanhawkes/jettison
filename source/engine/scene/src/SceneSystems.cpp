#include <jettison/scene/SceneSystems.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <jettison/animation/Components.h>
#include <jettison/animation/Playback.h>
#include <jettison/core/Log.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>

namespace Jettison::Scene
{
using Animation::AnimationPlayerComponent;
using Animation::RotationAnimationComponent;
using Animation::SkeletonComponent;

namespace
{
constexpr Core::Category kScene{"scene"};

glm::dmat4 Finish(
    entt::entity entity, entt::registry &registry, glm::dmat4 local,
    std::unordered_map<entt::entity, glm::dmat4> &computed, std::unordered_set<entt::entity> &active);

glm::dmat4 ComputeWorldTransform(
    entt::entity entity, entt::registry &registry,
    std::unordered_map<entt::entity, glm::dmat4> &computed, std::unordered_set<entt::entity> &active)
{
	if (const auto found = computed.find(entity); found != computed.end())
		return found->second;
	if (!active.insert(entity).second)
		throw std::runtime_error("Scene hierarchy contains a parent cycle");

	// In double precision throughout, so a child's offset from a parent a hundred million
	// kilometres out keeps its millimetres.
	const auto &transform = registry.get<TransformComponent>(entity);
	glm::dmat4 local{1.0};
	if (const auto *placement = registry.try_get<PlacementComponent>(entity))
	{
		local = glm::translate(local, placement->position) * glm::mat4_cast(placement->orientation);
		local = glm::scale(local, glm::dvec3{transform.scale});
		return Finish(entity, registry, local, computed, active);
	}
	local = glm::translate(local, transform.translation);
	// The rotation animation's phase joins the rotation here, and is not accumulated in
	// TransformComponent: the authored rotation stays authored, so a scene saved from a running
	// engine writes the pose its author set, not the frame the animation happened to be on.
	glm::vec3 rotation = transform.rotation;
	if (const auto *animation = registry.try_get<RotationAnimationComponent>(entity))
		rotation += animation->rotationAxis * animation->angle;
	local = glm::rotate(local, double{rotation.x}, {1.0, 0.0, 0.0});
	local = glm::rotate(local, double{rotation.y}, {0.0, 1.0, 0.0});
	local = glm::rotate(local, double{rotation.z}, {0.0, 0.0, 1.0});
	local = glm::scale(local, glm::dvec3{transform.scale});
	return Finish(entity, registry, local, computed, active);
}

glm::dmat4 Finish(
    entt::entity entity, entt::registry &registry, glm::dmat4 local,
    std::unordered_map<entt::entity, glm::dmat4> &computed, std::unordered_set<entt::entity> &active)
{
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

// Whether the entity's clip can play on this rig: the rig has the clip and a skin for it to move.
// Looked up once a revision, and a clip that is missing reported then, since a file that names one
// is content the scene loaded without a word: the rig arrives later, on the I/O worker.
bool CheckClip(
    const entt::registry &registry, entt::entity entity, const Animation::Rig &rig,
    const MeshComponent &mesh)
{
	const auto *player = registry.try_get<AnimationPlayerComponent>(entity);
	if (player == nullptr)
		return false;
	if (rig.HasSkin() && rig.HasAnimation(player->clipName))
		return true;
	const auto *id = registry.try_get<SceneEntityId>(entity);
	Core::Log(
	    Core::Severity::Error, kScene,
	    "Entity '{}' plays clip '{}', which {} does not have with a skin to move: posed at rest",
	    id != nullptr ? std::string_view{id->value} : std::string_view{"(unnamed)"}, player->clipName,
	    mesh.model);
	return false;
}
} // namespace

void SceneUpdateSystem::Update(entt::registry &registry, float deltaTime) const
{
	const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
	for (const entt::entity entity : registry.view<TransformComponent, RotationAnimationComponent>())
		Animation::AdvanceRotation(registry.get<RotationAnimationComponent>(entity), frameTime);
	if (m_pResources != nullptr)
		for (const auto [entity, mesh, rigComponent] :
		     registry.view<const MeshComponent, RigComponent>().each())
		{
			const Animation::Rig *rig = m_pResources->TryGet(rigComponent.rig);
			if (rig == nullptr)
				continue;
			if (const std::uint32_t revision = m_pResources->RevisionOf(rigComponent.rig);
			    revision != rigComponent.revision)
			{
				rigComponent.playable = CheckClip(registry, entity, *rig, mesh);
				rigComponent.revision = revision;
			}
			auto &skeleton = registry.get_or_emplace<SkeletonComponent>(entity);
			std::string_view clipName;
			float animationTime = 0.0f;
			if (auto *player = registry.try_get<AnimationPlayerComponent>(entity);
			    player != nullptr && rigComponent.playable)
			{
				Animation::AdvancePlayer(*player, rig->AnimationDuration(player->clipName), frameTime);
				clipName = player->clipName;
				animationTime = player->time;
			}
			rig->EvaluateSkin(
			    clipName, animationTime, skeleton.jointMatrices, skeleton.meshNodeTransform);
		}

	std::unordered_map<entt::entity, glm::dmat4> computed;
	std::unordered_set<entt::entity> active;
	for (const entt::entity entity : registry.view<TransformComponent>())
		ComputeWorldTransform(entity, registry, computed, active);
}

void UpdateOrbitCameras(entt::registry &registry, double deltaTime)
{
	for (const auto [entity, orbit] : registry.view<OrbitCameraComponent>().each())
	{
		if (!orbit.placed)
		{
			OrbitCamera::Settings settings;
			settings.minDistance = orbit.minDistance;
			orbit.rig = OrbitCamera{settings};
			orbit.rig.Place(orbit.yaw, orbit.pitch, orbit.distance);
			orbit.placed = true;
		}
		if (const auto *world = registry.valid(orbit.target)
		                            ? registry.try_get<WorldTransformComponent>(orbit.target)
		                            : nullptr)
			orbit.rig.SetTarget(glm::dvec3{world->matrix[3]});
		const CameraPose rigPose = orbit.rig.Update(deltaTime);
		const CameraPose pose = orbit.transition.Update(rigPose, deltaTime);
		registry.emplace_or_replace<CameraPoseComponent>(entity, pose.position, pose.orientation);
	}
}
} // namespace Jettison::Scene
