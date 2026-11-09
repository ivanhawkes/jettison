#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <filesystem>
#include <unordered_map>
#include <utility>
#include <vector>

#include "DeviceContext.h"
#include "SceneSystems.h"
#include "Swapchain.h"


namespace Jettison::Renderer
{
class ImGuiRenderer;

class Pipeline
{
  public:
	Pipeline(
	    std::shared_ptr<DeviceContext> pDeviceContext,
	    std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, std::filesystem::path assetDirectory)
	    : m_pDeviceContext{pDeviceContext}, m_pSwapchain{pSwapchain},
	      m_assetDirectory{std::move(assetDirectory)}
	{
	}

	// Disable copying.
	Pipeline() = default;
	Pipeline(const Pipeline &) = delete;
	Pipeline &operator=(const Pipeline &) = delete;

	void Init();

	void Recreate();

	void Destroy();

	void CreateCommandBuffers();

	void RecordCommandBuffer(
	    uint32_t imageIndex, const std::vector<SceneDrawCommand> &drawCommands,
	    const ImGuiRenderer &imguiRenderer);

	VkRenderPass GetRenderPass() const
	{
		return m_renderPass;
	}

	inline const std::vector<VkCommandBuffer> &GetCommandBuffers() const
	{
		return m_commandBuffers;
	}
	inline const std::vector<VkDeviceMemory> &GetUniformBuffersMemory() const
	{
		return m_uniformBuffersMemory;
	}

  private:
	struct TextureResource
	{
		VkImage image{VK_NULL_HANDLE};
		VkDeviceMemory memory{VK_NULL_HANDLE};
		VkImageView view{VK_NULL_HANDLE};
		uint32_t mipLevels{};
	};
	struct ModelResources
	{
		TextureResource texture;
		VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
		std::vector<VkDescriptorSet> descriptorSets;
		bool ownsTexture{};
	};

	void Create();

	void CreateRenderPass();

	void CreateGraphicsPipeline();

	void CreateFramebuffers();

	void CreateImageViews();

	void CreateUniformBuffers();
	void EnsureJointBuffer(uint32_t imageIndex, std::size_t jointCount);

	void CreateDescriptorSetLayout();

	void CreateDescriptorPool();

	void CreateDescriptorSets();

	void CreateTextureImage();
	void CreateTextureImageData(
	    const unsigned char *pixels, int width, int height, TextureResource &texture);

	void CreateTextureImageView();

	void CreateTextureSampler();
	ModelResources &EnsureModelResources(const Model &model, std::size_t primitiveIndex);

	void CreateColorResources();

	void CreateDepthResources();

	void GenerateMipmaps(
	    VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);

	static std::vector<char> ReadFile(const std::string &filename);

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext{nullptr};

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain{nullptr};
	std::filesystem::path m_assetDirectory;

	VkRenderPass m_renderPass{VK_NULL_HANDLE};

	VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
	VkPipeline m_graphicsPipeline{VK_NULL_HANDLE};

	std::vector<VkFramebuffer> m_swapchainFramebuffers{};

	std::vector<VkCommandBuffer> m_commandBuffers{};

	VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
	std::vector<VkDescriptorSet> m_descriptorSets{};
	VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};

	std::vector<VkImage> m_swapchainImages{};
	std::vector<VkImageView> m_swapchainImageViews{};

	std::vector<VkBuffer> m_uniformBuffers{};
	std::vector<VkDeviceMemory> m_uniformBuffersMemory{};
	std::vector<VkBuffer> m_jointBuffers{};
	std::vector<VkDeviceMemory> m_jointBuffersMemory{};
	std::vector<VkDeviceSize> m_jointBufferSizes{};

	VkImage m_colorImage{VK_NULL_HANDLE};
	VkDeviceMemory m_colorImageMemory{nullptr};
	VkImageView m_colorImageView{VK_NULL_HANDLE};

	VkImage m_depthImage{VK_NULL_HANDLE};
	VkDeviceMemory m_depthImageMemory{VK_NULL_HANDLE};
	VkImageView m_depthImageView{VK_NULL_HANDLE};

	TextureResource m_fallbackTexture;
	VkSampler m_textureSampler{VK_NULL_HANDLE};
	std::unordered_map<const Model *, std::vector<ModelResources>> m_modelResources;
};
} // namespace Jettison::Renderer
