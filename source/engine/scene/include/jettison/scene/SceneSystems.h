#pragma once

#include <entt/entt.hpp>

namespace Jettison::Resource
{
class ResourceManager;
}

namespace Jettison::Scene
{
// The presentation phase's scene update: rotation animations and clip players advanced, each model
// posed from its rig, then every world transform composed in double precision. A model's rig loads
// through the resource manager (RigComponent); until it is ready the entity has no
// SkeletonComponent, and the renderer does not draw it, since the rig is what places the mesh within
// its file. A clip the rig does not have is reported once, and the model is posed at rest.
class SceneUpdateSystem
{
  public:
	// The rigs are read from the resource manager, which outlives the system; without one, no model
	// is posed.
	explicit SceneUpdateSystem(const Resource::ResourceManager *pResources = nullptr)
	    : m_pResources{pResources}
	{
	}

	void Update(entt::registry &registry, float deltaTime) const;

  private:
	const Resource::ResourceManager *m_pResources;
};

// The presentation phase's camera rigs: each OrbitCameraComponent follows its target's world
// position (so it runs after the world transforms) and writes the camera's CameraPoseComponent,
// through any transition it is in.
void UpdateOrbitCameras(entt::registry &registry, double deltaTime);
} // namespace Jettison::Scene
