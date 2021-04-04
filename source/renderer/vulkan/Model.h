#pragma once

#include <vulkan/vulkan.h>

// GL Math.
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <../glm/glm/glm.hpp>
#include <../glm/glm/gtc/matrix_transform.hpp>
#include <../glm/glm/gtx/hash.hpp>

// STD.
#include <vector>

#include "DeviceContext.h"


namespace Jettison::Renderer
{
struct ModelUBO
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 projection;
};


struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;

	static VkVertexInputBindingDescription getBindingDescription()
	{
		VkVertexInputBindingDescription bindingDescription {};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return bindingDescription;
	}


	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
	{
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions {
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
			{0, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
			{0, 2, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, texCoord)}
		};

		return attributeDescriptions;
	}


	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}
};


class Model
{
public:
	Model(std::shared_ptr<DeviceContext> pDeviceContext)
		:m_pDeviceContext {pDeviceContext} {}

	// Disable copying.
	Model() = default;
	Model(const Model&) = delete;
	Model& operator=(const Model&) = delete;

	void Destroy();

	void LoadModel();

	std::vector<uint32_t> m_indices {};
	VkBuffer m_vertexBuffer {VK_NULL_HANDLE};
	VkBuffer m_indexBuffer {VK_NULL_HANDLE};

private:
	void CreateVertexBuffer();

	void CreateIndexBuffer();

	std::shared_ptr<DeviceContext> m_pDeviceContext;

	std::vector<Vertex> m_vertices {};
	VkDeviceMemory m_vertexBufferMemory {VK_NULL_HANDLE};
	VkDeviceMemory m_indexBufferMemory {VK_NULL_HANDLE};
};
}


namespace std
{
template<> struct hash<Jettison::Renderer::Vertex>
{
	size_t operator()(const Jettison::Renderer::Vertex& vertex) const
	{
		return ((hash<glm::vec3>()(vertex.pos) ^
			(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
			(hash<glm::vec2>()(vertex.texCoord) << 1);
	}
};
}

