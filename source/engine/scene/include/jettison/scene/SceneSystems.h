#pragma once

#include <entt/entt.hpp>

#include <map>
#include <string>
#include <vector>

#include <jettison/animation/ClipFile.h>
#include <jettison/resource/ResourceHandle.h>
#include <jettison/scene/Sequence.h>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Scene
{
// An event of a clip crossed in a frame (Animation::ClipEvent): the entity whose ClipPlayerComponent
// played it, and the event's name, for the game to hear: a clamp closing, for a sound.
struct ClipEventFired
{
	entt::entity player{entt::null};
	std::string name;
};

// The presentation phase's scene update: rotation animations and clip players advanced, each model
// posed from its rig and each clip file's clip sampled onto the entities bound to it
// (NodePoseComponent), then every world transform composed in double precision. A model's rig loads
// through the resource manager (RigComponent); until it is ready the entity has no
// SkeletonComponent, and the renderer does not draw it, since the rig is what places the mesh within
// its file. A clip the rig does not have is reported once, and the model is posed at rest. Last, each
// entity of a layer the scene hides is tagged HiddenComponent (Scene::SetLayerVisible).
class SceneUpdateSystem
{
  public:
	// The rigs and the clip files are read from the resource manager, which outlives the system;
	// without one, no model is posed and no clip played.
	explicit SceneUpdateSystem(Resource::ResourceManager *pResources = nullptr)
	    : m_pResources{pResources}
	{
	}
	~SceneUpdateSystem();
	SceneUpdateSystem(const SceneUpdateSystem &) = delete;
	SceneUpdateSystem &operator=(const SceneUpdateSystem &) = delete;

	// Appends the clip events crossed this frame to events, if given, in the order of the players
	// and, for each, the order played.
	void Update(
	    entt::registry &registry, float deltaTime,
	    std::vector<ClipEventFired> *pEvents = nullptr) const;

  private:
	Resource::ResourceManager *m_pResources;
	// Each clip file a player has named, loaded as it is first named and held while the system
	// lives: a handful of small files a game, which a rig built again plays without waiting. A cache,
	// so an update that fills it is still const.
	mutable std::map<std::string, Resource::ResourceHandle<Animation::ClipFile>, std::less<>>
	    m_clipFiles;
};

// The presentation phase's camera rigs: each OrbitCameraComponent follows its target's world
// position (so it runs after the world transforms) and writes the camera's CameraPoseComponent,
// through any transition it is in.
void UpdateOrbitCameras(entt::registry &registry, double deltaTime);

// After the orbit cameras: each PilotCameraComponent's rigs follow their target's world transform,
// and the one its view names writes the camera's CameraPoseComponent, through any transition, over
// whatever the orbit rig wrote. A camera whose target has gone is left where it was.
void UpdatePilotCameras(entt::registry &registry, double deltaTime);

// An event of a sequence crossed in a frame: the camera whose SequencePlayerComponent played it, and
// the event's name, for the game to give a meaning.
struct SequenceEventFired
{
	entt::entity camera{entt::null};
	std::string name;
};

// After the camera rigs: each SequencePlayerComponent's sequence advanced by the frame's time and
// sampled over the pose its camera's rigs left (or its authored one), into the camera's
// CameraPoseComponent with its field of view, and what the frame shows of it into the player. A
// sequence is loaded through the resource manager as a player first names it, and held while the
// system lives; until it is in, and until each role it names is bound to an entity with a world
// transform, the player waits at its time, so a game may bind what it does not show yet. A file that
// fails, a role the sequence names and the player has no binding for at all, or, once it has
// started, an entity bound that is gone, is reported once and the player removed, the camera left to
// its rigs; so is a player whose sequence has ended.
class SequenceSystem
{
  public:
	explicit SequenceSystem(Resource::ResourceManager &resources) : m_resources{resources}
	{
	}
	~SequenceSystem();
	SequenceSystem(const SequenceSystem &) = delete;
	SequenceSystem &operator=(const SequenceSystem &) = delete;

	// Appends the events crossed this frame to events, if given, in the order of the cameras and,
	// for each, the order crossed.
	void Update(
	    entt::registry &registry, double deltaTime, std::vector<SequenceEventFired> *pEvents = nullptr);

  private:
	Resource::ResourceManager &m_resources;
	std::map<std::string, Resource::ResourceHandle<Sequence>, std::less<>> m_sequences;
};
} // namespace Jettison::Scene
