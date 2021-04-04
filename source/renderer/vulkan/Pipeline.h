#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <vector>

#include "DeviceContext.h"
#include "Model.h"
#include "Swapchain.h"


namespace Jettison::Renderer
{
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
	std::shared_ptr<DeviceContext> m_pDeviceContext {nullptr};

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
