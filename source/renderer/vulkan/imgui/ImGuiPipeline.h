#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "../DeviceContext.h"
#include "../Swapchain.h"


namespace Jettison::Renderer
{
class ImGuiPipeline
{
  public:
	const VkAllocationCallbacks *kPAllocator{nullptr};

	ImGuiPipeline(
	    std::shared_ptr<DeviceContext> pDeviceContext,
	    std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, std::filesystem::path assetDirectory)
	    : m_pDeviceContext{pDeviceContext}, m_pSwapchain{pSwapchain},
	      m_assetDirectory{std::move(assetDirectory)}
	{
	}

	// Disable copying.
	ImGuiPipeline() = default;
	ImGuiPipeline(const ImGuiPipeline &) = delete;
	ImGuiPipeline &operator=(const ImGuiPipeline &) = delete;

	void Init();

	void Recreate();

	void Destroy();

	VkQueue g_Queue{VK_NULL_HANDLE};
	VkDescriptorPool g_DescriptorPool{VK_NULL_HANDLE};

	bool g_SwapChainRebuild{false};

	VkPipelineCache g_PipelineCache{VK_NULL_HANDLE};

	void CreateOrResizeWindow(uint32_t queue_family);

	void CleanupVulkan();

	void CleanupVulkanWindow();

	void Render();

	void Present();

  private:
	struct Frame
	{
		VkCommandPool CommandPool{VK_NULL_HANDLE};
		VkCommandBuffer CommandBuffer{VK_NULL_HANDLE};
		VkFence Fence{VK_NULL_HANDLE};
		VkImage Backbuffer{VK_NULL_HANDLE};
		VkImageView BackbufferView{VK_NULL_HANDLE};
		VkFramebuffer Framebuffer{VK_NULL_HANDLE};
	};

	struct FrameSemaphores
	{
		VkSemaphore ImageAcquiredSemaphore{VK_NULL_HANDLE};
		VkSemaphore RenderCompleteSemaphore{VK_NULL_HANDLE};
	};

	struct WindowData
	{
		VkSurfaceKHR Surface{VK_NULL_HANDLE};
		VkSurfaceFormatKHR SurfaceFormat{};
		VkPresentModeKHR PresentMode{};
		VkClearValue ClearValue{};
		int Width{0};
		int Height{0};
		VkSwapchainKHR Swapchain{VK_NULL_HANDLE};
		VkRenderPass RenderPass{VK_NULL_HANDLE};
		VkPipeline Pipeline{VK_NULL_HANDLE};
		uint32_t FrameIndex{0};
		uint32_t ImageCount{0};
		uint32_t SemaphoreIndex{0};
		std::vector<Frame> Frames;
		std::vector<FrameSemaphores> Semaphores;
	};

	void Create();

	void CreateRenderPass();

	void CreateFramebuffers();

	void CreateImageViews();

	void CreateDescriptorPool();

	void InitVulkanBackend();

	int GetMinImageCountFromPresentMode(VkPresentModeKHR present_mode);

	void DestroyFrame(Frame *fd);

	void DestroyFrameSemaphores(FrameSemaphores *fsd);

	void DestroyWindow();

	void CreateWindowSwapChain();

	void CreateWindowCommandBuffers();

	void SetupVulkan();

	void SetupVulkanWindow();

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext{nullptr};

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain{nullptr};
	std::filesystem::path m_assetDirectory;

	WindowData m_windowData{};
	std::vector<VkFence> m_acquireFences;
	bool m_suboptimal{false};
};
} // namespace Jettison::Renderer
