#include <jettison/scene/SceneSystems.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <jettison/animation/ClipFile.h>
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
	// A clip's pose replaces the authored transform path by path, and the rotation animation's
	// phase joins the rotation here: neither is accumulated in TransformComponent, so the authored
	// transform stays authored, and a scene saved from a running engine writes the pose its author
	// set, not the frame an animation happened to be on.
	const auto *pose = registry.try_get<NodePoseComponent>(entity);
	const auto *animation = registry.try_get<RotationAnimationComponent>(entity);
	local = glm::translate(
	    local, pose != nullptr && pose->translated ? pose->translation : transform.translation);
	if (pose != nullptr && pose->rotated)
	{
		local *= glm::dmat4{glm::mat4_cast(pose->rotation)};
		if (animation != nullptr)
			local = glm::rotate(local, double{animation->angle}, glm::dvec3{animation->rotationAxis});
	}
	else
	{
		glm::vec3 rotation = transform.rotation;
		if (animation != nullptr)
			rotation += animation->rotationAxis * animation->angle;
		local = glm::rotate(local, double{rotation.x}, {1.0, 0.0, 0.0});
		local = glm::rotate(local, double{rotation.y}, {0.0, 1.0, 0.0});
		local = glm::rotate(local, double{rotation.z}, {0.0, 0.0, 1.0});
	}
	local =
	    glm::scale(local, glm::dvec3{pose != nullptr && pose->scaled ? pose->scale : transform.scale});
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

std::string_view NameOf(const entt::registry &registry, entt::entity entity)
{
	const auto *id = registry.try_get<SceneEntityId>(entity);
	return id != nullptr ? std::string_view{id->value} : std::string_view{"(unnamed)"};
}

// Whether the entity's clip can play on this rig: the rig has the clip. Looked up once a revision,
// and a clip that is missing reported then, since a file that names one is content the scene loaded
// without a word: the rig arrives later, on the I/O worker. A rig with no skin plays its clip too,
// which moves its nodes and so where its mesh is drawn.
bool CheckClip(
    const entt::registry &registry, entt::entity entity, const Animation::Rig &rig,
    const MeshComponent &mesh)
{
	const auto *player = registry.try_get<AnimationPlayerComponent>(entity);
	if (player == nullptr)
		return false;
	if (rig.HasAnimation(player->clipName))
		return true;
	Core::Log(
	    Core::Severity::Error, kScene,
	    "Entity '{}' plays clip '{}', which {} does not have: posed at rest", NameOf(registry, entity),
	    player->clipName, mesh.model);
	return false;
}

// What keeps the player's clip from playing on what it is bound to, or nothing: the file lacks the
// clip, a binding is to a node the file does not name or to an entity with no transform, or a node
// the clip moves is bound to nothing.
std::string ProblemWith(
    const entt::registry &registry, const ClipPlayerComponent &player, const Animation::ClipFile &file)
{
	const Animation::Clip *clip = Animation::FindClip(file.clips, player.clip);
	if (clip == nullptr)
		return std::format("which {} does not have", player.file);
	for (const ClipBinding &binding : player.nodes)
	{
		if (Animation::NodeIndex(file, binding.node) == file.nodes.size())
			return std::format("binding node '{}', which {} does not name", binding.node, player.file);
		if (!registry.valid(binding.entity) || !registry.all_of<TransformComponent>(binding.entity))
			return std::format("binding node '{}' to an entity with no transform", binding.node);
	}
	for (const Animation::Track &track : clip->tracks)
	{
		const std::string &node = file.nodes[track.nodeIndex];
		if (std::ranges::none_of(
		        player.nodes, [&](const ClipBinding &binding) { return binding.node == node; }))
			return std::format("whose node '{}' is bound to nothing", node);
	}
	return {};
}

// Whether the player's clip can play on what it is bound to. Checked once for each revision of the
// file and each clip the player is set to, and what is wrong reported then, as a rig's clip is.
bool CheckPlayer(
    const entt::registry &registry, entt::entity entity, const ClipPlayerComponent &player,
    const Animation::ClipFile &file)
{
	const std::string problem = ProblemWith(registry, player, file);
	if (problem.empty())
		return true;
	Core::Log(
	    Core::Severity::Error, kScene, "Entity '{}' plays clip '{}', {}: left as authored",
	    NameOf(registry, entity), player.clip, problem);
	return false;
}

// The pose's path the track moves, set to its value.
void Pose(NodePoseComponent &pose, const Animation::Track &track, const glm::vec4 &value)
{
	switch (track.path)
	{
		case Animation::TrackPath::Translation:
			pose.translation = glm::dvec3{glm::vec3{value}};
			pose.translated = true;
			break;
		case Animation::TrackPath::Rotation:
			pose.rotation = glm::normalize(glm::quat{value.w, value.x, value.y, value.z});
			pose.rotated = true;
			break;
		case Animation::TrackPath::Scale:
			pose.scale = glm::vec3{value};
			pose.scaled = true;
			break;
	}
}

// The layer an entity is in: its own, or else its parent's, up to the environment at a root.
Layer LayerOf(
    entt::entity entity, const entt::registry &registry, std::unordered_map<entt::entity, Layer> &found)
{
	if (const auto known = found.find(entity); known != found.end())
		return known->second;
	Layer layer = Layer::Environment;
	if (const auto *own = registry.try_get<LayerComponent>(entity))
		layer = own->layer;
	else if (const auto *parent = registry.try_get<ParentComponent>(entity))
		// The world transforms are composed first, and refuse a cycle or a parent that is gone.
		layer = LayerOf(parent->entity, registry, found);
	found.emplace(entity, layer);
	return layer;
}

// Every entity of a hidden layer tagged HiddenComponent, and every other one not.
void ApplyLayerVisibility(entt::registry &registry)
{
	registry.clear<HiddenComponent>();
	const auto *visibility = registry.ctx().find<LayerVisibility>();
	if (visibility == nullptr || visibility->visible == LayerVisibility{}.visible)
		return;
	std::unordered_map<entt::entity, Layer> found;
	for (const entt::entity entity : registry.view<TransformComponent>())
		if (!visibility->IsVisible(LayerOf(entity, registry, found)))
			registry.emplace<HiddenComponent>(entity);
}
} // namespace

SceneUpdateSystem::~SceneUpdateSystem()
{
	for (const auto &[path, handle] : m_clipFiles)
		m_pResources->Release(handle);
}

void SceneUpdateSystem::Update(
    entt::registry &registry, float deltaTime, std::vector<ClipEventFired> *pEvents) const
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

	// Every pose made again from the clips playing this frame, so a node no clip is bound to any
	// more is back as authored.
	registry.clear<NodePoseComponent>();
	if (m_pResources != nullptr)
	{
		std::vector<std::size_t> crossed;
		for (const auto [entity, player] : registry.view<ClipPlayerComponent>().each())
		{
			auto found = m_clipFiles.find(player.file);
			if (found == m_clipFiles.end())
				found = m_clipFiles
				            .emplace(player.file, m_pResources->Load<Animation::ClipFile>(player.file))
				            .first;
			const Animation::ClipFile *file = m_pResources->TryGet(found->second);
			if (file == nullptr)
				continue;
			if (const std::uint32_t revision = m_pResources->RevisionOf(found->second);
			    revision != player.revision || player.clip != player.checked)
			{
				player.playable = CheckPlayer(registry, entity, player, *file);
				player.revision = revision;
				player.checked = player.clip;
			}
			if (!player.playable)
				continue;
			const Animation::Clip &clip = *Animation::FindClip(file->clips, player.clip);
			crossed.clear();
			player.time = Animation::AdvanceClip(
			    clip, player.time, player.playing ? frameTime * player.speed : 0.0f, player.looping,
			    crossed);
			if (pEvents != nullptr)
				for (const std::size_t index : crossed)
					pEvents->push_back({entity, clip.events[index].name});
			for (const Animation::Track &track : clip.tracks)
			{
				const glm::vec4 value = Animation::Sample(track, player.time);
				for (const ClipBinding &binding : player.nodes)
					if (binding.node == file->nodes[track.nodeIndex] && registry.valid(binding.entity))
						Pose(registry.get_or_emplace<NodePoseComponent>(binding.entity), track, value);
			}
		}
	}

	std::unordered_map<entt::entity, glm::dmat4> computed;
	std::unordered_set<entt::entity> active;
	for (const entt::entity entity : registry.view<TransformComponent>())
		ComputeWorldTransform(entity, registry, computed, active);
	ApplyLayerVisibility(registry);
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

void UpdatePilotCameras(entt::registry &registry, double deltaTime)
{
	for (const auto [entity, pilot] : registry.view<PilotCameraComponent>().each())
	{
		const auto *world = registry.valid(pilot.target)
		                        ? registry.try_get<WorldTransformComponent>(pilot.target)
		                        : nullptr;
		if (!world)
			continue;
		// The rotation without the scale, as the scene's debug axes take it.
		const glm::dmat3 basis{
		    glm::normalize(glm::dvec3{world->matrix[0]}), glm::normalize(glm::dvec3{world->matrix[1]}),
		    glm::normalize(glm::dvec3{world->matrix[2]})};
		const CameraPose target{glm::dvec3{world->matrix[3]}, glm::normalize(glm::quat_cast(basis))};
		pilot.chase.SetTarget(target);
		pilot.cockpit.SetTarget(target);
		const CameraPose chase = pilot.chase.Update(deltaTime);
		const CameraPose cockpit = pilot.cockpit.Update(deltaTime);
		const CameraPose pose =
		    pilot.transition.Update(pilot.view == PilotView::Chase ? chase : cockpit, deltaTime);
		registry.emplace_or_replace<CameraPoseComponent>(entity, pose.position, pose.orientation);
	}
}
} // namespace Jettison::Scene

namespace Jettison::Scene
{
SequenceSystem::~SequenceSystem()
{
	for (const auto &[path, handle] : m_sequences)
		m_resources.Release(handle);
}

void SequenceSystem::Update(
    entt::registry &registry, double deltaTime, std::vector<SequenceEventFired> *pEvents)
{
	// As the scene's update does, a long frame plays as a tenth of a second rather than jumping.
	const double frameTime = std::clamp(deltaTime, 0.0, 0.1);
	std::vector<entt::entity> ended;
	for (const auto [camera, player] : registry.view<SequencePlayerComponent>().each())
	{
		auto found = m_sequences.find(player.file);
		if (found == m_sequences.end())
			found = m_sequences.emplace(player.file, m_resources.Load<Sequence>(player.file)).first;
		const Resource::Status status = m_resources.StatusOf(found->second);
		if (status == Resource::Status::Failed)
		{
			Core::Log(
			    Core::Severity::Warning, kScene, "Sequence '{}' is not played: {}", player.file,
			    m_resources.ErrorOf(found->second));
			ended.push_back(camera);
			continue;
		}
		if (status != Resource::Status::Ready)
			continue;
		const Sequence &sequence = *m_resources.TryGet(found->second);

		// Where each role is this frame, after the world transforms.
		std::vector<glm::dmat4> frames;
		frames.reserve(sequence.roles.size());
		std::string_view unbound;
		bool absent = false;
		for (const std::string &role : sequence.roles)
		{
			const auto bound = player.roles.find(role);
			const auto *world = bound != player.roles.end() && registry.valid(bound->second)
			                        ? registry.try_get<WorldTransformComponent>(bound->second)
			                        : nullptr;
			if (world == nullptr)
			{
				unbound = role;
				absent = bound == player.roles.end();
				break;
			}
			frames.push_back(world->matrix);
		}
		if (!unbound.empty())
		{
			// Before its first frame, a role bound to nothing yet is waited for.
			if (!absent && !player.started)
				continue;
			Core::Log(
			    Core::Severity::Warning, kScene, "Sequence '{}' {}: its role '{}' is {}", player.file,
			    player.started ? "stops" : "is not played", unbound,
			    absent ? "not bound" : "bound to no entity of the scene with a transform");
			ended.push_back(camera);
			continue;
		}

		// The game's camera, where its rigs left it this frame, or where it was authored.
		const auto *authored = registry.try_get<CameraComponent>(camera);
		const CameraComponent fallback;
		const CameraComponent &own = authored != nullptr ? *authored : fallback;
		CameraPose game{
		    own.position, OrientationFromAngles(own.rotation.x, own.rotation.y, own.rotation.z)};
		if (const auto *pose = registry.try_get<CameraPoseComponent>(camera))
			game = {pose->position, pose->orientation};

		// Its first frame shows where it starts, set or not; its time runs from the next.
		std::vector<std::size_t> crossed;
		player.time = Animation::AdvanceClip(
		    sequence.timeline, static_cast<float>(player.time),
		    player.started ? static_cast<float>(frameTime) : 0.0f, false, crossed);
		player.started = true;
		if (pEvents != nullptr)
			for (const std::size_t event : crossed)
				pEvents->push_back({camera, sequence.timeline.events[event].name});

		const SequenceView view =
		    SampleSequence(sequence, player.time, frames, game, own.verticalFovDegrees);
		registry.emplace_or_replace<CameraPoseComponent>(
		    camera, view.pose.position, view.pose.orientation, view.verticalFovDegrees);
		player.fadeColor = sequence.fadeColor;
		player.fade = view.fade;
		player.letterbox = view.letterbox;
		player.weight = view.weight;
		if (player.time >= sequence.duration)
			ended.push_back(camera);
	}
	for (const entt::entity camera : ended)
		registry.remove<SequencePlayerComponent>(camera);
}
} // namespace Jettison::Scene
