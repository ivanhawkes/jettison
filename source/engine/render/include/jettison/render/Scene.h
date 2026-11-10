#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <jettison/animation/Components.h>
#include <jettison/render/vulkan/Model.h>

namespace Jettison::Render
{
struct SceneEntityId
{
	std::string value;
};

struct TransformComponent
{
	glm::vec3 translation{0.0f};
	glm::vec3 rotation{0.0f};
	glm::vec3 scale{1.0f};
};

struct ParentComponent
{
	entt::entity entity{entt::null};
};

struct WorldTransformComponent
{
	glm::mat4 matrix{1.0f};
};

struct CameraComponent
{
	glm::vec3 position{2.0f, 2.0f, 2.0f};
	// Scene coordinates are right-handed and Y-up. Radians: pitch about X, roll about view, yaw about
	// Y.
	glm::vec3 rotation{-0.615479709f, 0.0f, 0.785398163f};
	float verticalFovDegrees{45.0f};
	float nearPlane{0.1f};
	float farPlane{10.0f};
	bool active{true};
};

struct LightComponent
{
	glm::vec3 color{1.0f};
	float intensity{6.0f};
	glm::vec3 ambientColor{0.08f};
	bool active{true};
};

struct MeshComponent
{
	std::filesystem::path modelPath;
	std::shared_ptr<Model> model;
};

class Scene
{
  public:
	explicit Scene(std::shared_ptr<DeviceContext> deviceContext)
	    : m_deviceContext{std::move(deviceContext)}
	{
	}

	Scene(const Scene &) = delete;
	Scene &operator=(const Scene &) = delete;

	void Load(const std::filesystem::path &path);
	void Save(const std::filesystem::path &path) const;
	void Destroy();

	// Counts the times this scene's contents have been thrown away. Pipeline caches each model's
	// Vulkan resources under the address of that Model, and a destroyed model is commonly
	// followed by one allocated at the same address: a cache entry can therefore outlive the
	// model it describes, and be taken for the new one. This counter is how the renderer learns
	// the cache is stale, without the scene having to know who consumes it.
	std::uint64_t Generation() const
	{
		return m_generation;
	}

	entt::registry &Registry()
	{
		return m_registry;
	}
	const entt::registry &Registry() const
	{
		return m_registry;
	}

  private:
	std::shared_ptr<Model> LoadModel(const std::filesystem::path &path);

	std::shared_ptr<DeviceContext> m_deviceContext;
	entt::registry m_registry;
	std::unordered_map<std::string, std::shared_ptr<Model>> m_models;
	std::uint64_t m_generation{0};
};
} // namespace Jettison::Render