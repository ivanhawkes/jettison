#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

#include "vulkan/Model.h"

namespace Jettison::Renderer
{
struct SceneDrawCommand
{
	const Model *model{};
	glm::mat4 worldTransform{1.0f};
};

class SceneUpdateSystem
{
  public:
	void Update(entt::registry &registry, float deltaTime) const;
};

class SceneRenderSystem
{
  public:
	std::vector<SceneDrawCommand> Collect(const entt::registry &registry) const;
};
} // namespace Jettison::Renderer