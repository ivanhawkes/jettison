#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

#include "Scene.h"

namespace Jettison::Renderer
{
struct SceneDrawCommand
{
	const Model *model{};
	glm::mat4 worldTransform{1.0f};
	const SkeletonComponent *skeleton{};
};

class SceneUpdateSystem
{
	const SkeletonComponent *skeleton{};

  public:
	void Update(entt::registry &registry, float deltaTime) const;
};

class SceneRenderSystem
{
  public:
	std::vector<SceneDrawCommand> Collect(const entt::registry &registry) const;
};
} // namespace Jettison::Renderer