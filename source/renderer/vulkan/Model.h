#pragma once

#include <vulkan/vulkan.h>

// GL Math. GLM's configuration macros are set on the glm target in source/external, not here:
// they have to precede the first GLM include in every translation unit, which a header set
// them in can only do for the files that include it first.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

// STD.
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "DeviceContext.h"


namespace Jettison::Renderer
{
struct FrameUBO
{
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 projection;
	alignas(16) glm::vec4 cameraPosition;
	alignas(16) glm::vec4 lightPosition;
	alignas(16) glm::vec4 lightColorIntensity;
	alignas(16) glm::vec4 ambientColor;
};


struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
	glm::vec3 normal;
	glm::uvec4 joints{0};
	glm::vec4 weights{0.0f};
	glm::uvec4 joints1{0};
	glm::vec4 weights1{0.0f};

	static VkVertexInputBindingDescription getBindingDescription()
	{
		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return bindingDescription;
	}


	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
	{
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{
		    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
		    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
		    {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)},
		    {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
		    {4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, joints)},
		    {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights)},
		    {6, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, joints1)},
		    {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights1)}};

		return attributeDescriptions;
	}


	bool operator==(const Vertex &other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord &&
		       normal == other.normal && joints == other.joints && weights == other.weights &&
		       joints1 == other.joints1 && weights1 == other.weights1;
	}
};

enum class ModelAnimationPath
{
	Translation,
	Rotation,
	Scale
};

enum class ModelAnimationInterpolation
{
	Linear,
	Step,
	CubicSpline
};

struct ModelNode
{
	int parent{-1};
	glm::vec3 translation{0.0f};
	glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 scale{1.0f};
};

struct ModelAnimationTrack
{
	std::size_t nodeIndex{};
	ModelAnimationPath path{ModelAnimationPath::Translation};
	ModelAnimationInterpolation interpolation{ModelAnimationInterpolation::Linear};
	std::vector<float> times;
	std::vector<glm::vec4> values;
};

struct ModelAnimationClip
{
	std::string name;
	float duration{};
	std::vector<ModelAnimationTrack> tracks;
};

struct ModelSkinJoint
{
	std::size_t nodeIndex{};
	glm::mat4 inverseBindMatrix{1.0f};
};

struct ModelPrimitive
{
	uint32_t firstIndex{};
	uint32_t indexCount{};
	std::vector<std::byte> baseColorImageData;
};


class Model
{
  public:
	Model(std::shared_ptr<DeviceContext> pDeviceContext) : m_pDeviceContext{pDeviceContext}
	{
	}

	// Disable copying.
	Model() = default;
	Model(const Model &) = delete;
	Model &operator=(const Model &) = delete;

	void Destroy();

	void LoadModel(const std::filesystem::path &path);
	bool HasSkin() const
	{
		return !m_joints.empty();
	}
	bool HasSceneGraph() const
	{
		return !m_nodes.empty();
	}
	bool HasAnimation(std::string_view name) const;
	float AnimationDuration(std::string_view name) const;
	void EvaluateSkin(
	    std::string_view animationName, float time, std::vector<glm::mat4> &jointMatrices,
	    glm::mat4 &meshNodeTransform) const;

	std::vector<uint32_t> m_indices{};
	std::vector<ModelPrimitive> m_primitives{};
	VkBuffer m_vertexBuffer{VK_NULL_HANDLE};
	VkBuffer m_indexBuffer{VK_NULL_HANDLE};

  private:
	void LoadGltfModel(const std::filesystem::path &path);
	void CreateVertexBuffer();

	void CreateIndexBuffer();

	std::shared_ptr<DeviceContext> m_pDeviceContext;

	std::vector<Vertex> m_vertices{};
	std::vector<ModelNode> m_nodes{};
	std::vector<ModelSkinJoint> m_joints{};
	std::vector<ModelAnimationClip> m_animations{};
	std::size_t m_meshNodeIndex{};
	VkDeviceMemory m_vertexBufferMemory{VK_NULL_HANDLE};
	VkDeviceMemory m_indexBufferMemory{VK_NULL_HANDLE};
};
} // namespace Jettison::Renderer


namespace std
{
template <> struct hash<Jettison::Renderer::Vertex>
{
	size_t operator()(const Jettison::Renderer::Vertex &vertex) const
	{
		return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
		       (hash<glm::vec2>()(vertex.texCoord) << 1) ^ (hash<glm::vec3>()(vertex.normal) << 2) ^
		       (hash<glm::uvec4>()(vertex.joints) << 3) ^ (hash<glm::vec4>()(vertex.weights) << 4) ^
		       (hash<glm::uvec4>()(vertex.joints1) << 5) ^ (hash<glm::vec4>()(vertex.weights1) << 6);
	}
};
} // namespace std
