#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

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
};

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