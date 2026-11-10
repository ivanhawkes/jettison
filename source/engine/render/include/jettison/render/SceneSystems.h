#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <string>
#include <vector>

#include <jettison/render/Camera.h>
#include <jettison/render/Scene.h>

namespace Jettison::Render
{
struct SceneDrawCommand
{
	const Model *model{};
	// In render space: metres from the camera, as a float.
	glm::mat4 renderTransform{1.0f};
	const Animation::SkeletonComponent *skeleton{};
	// The mesh's material path; empty for the model's own. Points into the scene, which outlives
	// the frame's draw list.
	const std::string *material{};
};

// The presentation phase's camera rigs: each OrbitCameraComponent follows its target's world
// position (so it runs after the world transforms) and writes the camera's CameraPoseComponent,
// through any transition it is in.
void UpdateOrbitCameras(entt::registry &registry, double deltaTime);

// The active camera's view this frame: its pose from CameraPoseComponent when a rig wrote one,
// otherwise from the authored CameraComponent. The default view when the scene has no active
// camera.
CameraView ActiveCameraView(const entt::registry &registry);

class SceneUpdateSystem
{
  public:
	void Update(entt::registry &registry, float deltaTime) const;
};

class SceneRenderSystem
{
  public:
	// Every mesh, made camera-relative against the camera's position.
	std::vector<SceneDrawCommand> Collect(
	    const entt::registry &registry, const glm::dvec3 &camera) const;
};
} // namespace Jettison::Render