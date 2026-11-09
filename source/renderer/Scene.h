#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "vulkan/Model.h"

namespace Jettison::Renderer
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
	// Scene coordinates are right-handed and Y-up. Radians: pitch about X, roll about view, yaw about Y.
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

struct RotationAnimationComponent
{
	glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
	float rotationSpeed{0.785398163f};
};

struct SkeletonComponent
{
	std::vector<glm::mat4> jointMatrices;
	glm::mat4 meshNodeTransform{1.0f};
};

struct AnimationPlayerComponent
{
	std::string clipName;
	float time{0.0f};
	float playbackSpeed{1.0f};
	bool looping{true};
	bool playing{true};
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

	entt::registry &Registry() { return m_registry; }
	const entt::registry &Registry() const { return m_registry; }

  private:
	std::shared_ptr<Model> LoadModel(const std::filesystem::path &path);

	std::shared_ptr<DeviceContext> m_deviceContext;
	entt::registry m_registry;
	std::unordered_map<std::string, std::shared_ptr<Model>> m_models;
};
} // namespace Jettison::Renderer