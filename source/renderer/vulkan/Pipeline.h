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
#include <array>
#include <vector>

#include "DeviceContext.h"
#include "Swapchain.h"


namespace Jettison::Renderer
{
struct UniformBufferObject
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


	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
	{
		std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions {};
		attributeDescriptions[0].binding = 0;
		attributeDescriptions[0].location = 0;
		attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[0].offset = offsetof(Vertex, pos);

		attributeDescriptions[1].binding = 0;
		attributeDescriptions[1].location = 1;
		attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attributeDescriptions[1].offset = offsetof(Vertex, color);

		attributeDescriptions[2].binding = 0;
		attributeDescriptions[2].location = 2;
		attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

		return attributeDescriptions;
	}


	bool operator==(const Vertex& other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord;
	}
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


namespace Jettison::Renderer
{
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


class Pipeline
{
public:
	Pipeline(std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain)
		:m_pDeviceContext {pDeviceContext}, m_pSwapchain {pSwapchain} {}

	// Disable copying.
	Pipeline() = default;
	Pipeline(const Pipeline&) = delete;
	Pipeline& operator=(const Pipeline&) = delete;

	void Init();

	void Recreate();

	void Destroy();

	void CreateCommandBuffers(const Model& model);

	inline const std::vector<VkCommandBuffer>& GetCommandBuffers() const { return m_commandBuffers; }
	inline const std::vector<VkDeviceMemory>& GetUniformBuffersMemory() const { return m_uniformBuffersMemory; }

private:
	void Create();

	void CreateRenderPass();

	void CreateGraphicsPipeline();

	void CreateFramebuffers();

	void CreateImageViews();

	void CreateUniformBuffers();

	void CreateDescriptorSetLayout();

	void CreateDescriptorPool();

	void CreateDescriptorSets();

	void CreateTextureImage();

	void CreateTextureImageView();

	void CreateTextureSampler();

	void CreateColorResources();

	void CreateDepthResources();

	void GenerateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

	static std::vector<char> ReadFile(const std::string& filename);

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext;

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain {nullptr};

	VkRenderPass m_renderPass {VK_NULL_HANDLE};

	VkPipelineLayout m_pipelineLayout {VK_NULL_HANDLE};
	VkPipeline m_graphicsPipeline {VK_NULL_HANDLE};

	std::vector<VkFramebuffer> m_swapchainFramebuffers {};

	std::vector<VkCommandBuffer> m_commandBuffers {};

	VkDescriptorPool m_descriptorPool {VK_NULL_HANDLE};
	std::vector<VkDescriptorSet> m_descriptorSets {};
	VkDescriptorSetLayout m_descriptorSetLayout {VK_NULL_HANDLE};

	std::vector<VkImage> m_swapchainImages {};
	std::vector<VkImageView> m_swapchainImageViews {};

	std::vector<VkBuffer> m_uniformBuffers {};
	std::vector<VkDeviceMemory> m_uniformBuffersMemory {};

	VkImage m_colorImage {VK_NULL_HANDLE};
	VkDeviceMemory m_colorImageMemory {nullptr};
	VkImageView m_colorImageView {VK_NULL_HANDLE};

	VkImage m_depthImage {VK_NULL_HANDLE};
	VkDeviceMemory m_depthImageMemory {VK_NULL_HANDLE};
	VkImageView m_depthImageView {VK_NULL_HANDLE};

	uint32_t m_mipLevels {0};
	VkImage m_textureImage {VK_NULL_HANDLE};
	VkDeviceMemory m_textureImageMemory {VK_NULL_HANDLE};
	VkImageView m_textureImageView {VK_NULL_HANDLE};
	VkSampler m_textureSampler {VK_NULL_HANDLE};
};
}
