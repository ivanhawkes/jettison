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

CameraView ActiveCameraView(const entt::registry &registry)
{
	CameraView view;
	const CameraComponent fallback;
	const CameraComponent *camera = &fallback;
	entt::entity cameraEntity = entt::null;
	for (const entt::entity entity : registry.view<const CameraComponent>())
		if (registry.get<CameraComponent>(entity).active)
		{
			camera = &registry.get<CameraComponent>(entity);
			cameraEntity = entity;
			break;
		}
	view.verticalFov = glm::radians(camera->verticalFovDegrees);
	view.nearPlane = camera->nearPlane;
	if (const auto *pose =
	        cameraEntity == entt::null ? nullptr : registry.try_get<CameraPoseComponent>(cameraEntity))
	{
		view.position = pose->position;
		view.orientation = pose->orientation;
	}
	else
	{
		view.position = camera->position;
		view.orientation =
		    OrientationFromAngles(camera->rotation.x, camera->rotation.y, camera->rotation.z);
	}
	// An orbit camera moves with what it circles, which is where the shadows' grid is fixed.
	if (const auto *orbit =
	        cameraEntity == entt::null ? nullptr : registry.try_get<OrbitCameraComponent>(cameraEntity))
		if (const auto *world = registry.valid(orbit->target)
		                            ? registry.try_get<WorldTransformComponent>(orbit->target)
		                            : nullptr)
			view.frameOrigin = glm::dvec3{world->matrix[3]};
	return view;
}

std::vector<SceneDrawCommand> SceneRenderSystem::Collect(
    const entt::registry &registry, const glm::dvec3 &camera) const
{
	std::vector<SceneDrawCommand> drawCommands;
	const auto meshes = registry.view<const MeshComponent, const WorldTransformComponent>();
	for (const entt::entity entity : meshes)
	{
		const auto &mesh = meshes.get<const MeshComponent>(entity);
		if (mesh.model)
		{
			const auto *skeleton = registry.try_get<SkeletonComponent>(entity);
			const glm::dmat4 meshNodeTransform{
			    skeleton ? skeleton->meshNodeTransform : glm::mat4{1.0f}};
			drawCommands.push_back(
			    {mesh.model.get(),
			     ToRenderSpace(
			         meshes.get<const WorldTransformComponent>(entity).matrix * meshNodeTransform,
			         camera),
			     skeleton, &mesh.material});
		}
	}
	return drawCommands;
}
} // namespace Jettison::Render