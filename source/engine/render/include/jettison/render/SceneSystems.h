#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

#include <jettison/render/Scene.h>

namespace Jettison::Render
{
struct SceneDrawCommand
{
	const Model *model{};
	glm::mat4 worldTransform{1.0f};
	const Animation::SkeletonComponent *skeleton{};
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
} // namespace Jettison::Render