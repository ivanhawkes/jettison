#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <iostream>
#include <stdexcept>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "../DeviceContext.h"
#include "../Swapchain.h"


namespace Jettison::Renderer
{
// TODO: Remove this - it's a copy from the ImGui vulkan implementation.
// Reusable buffers used for rendering 1 current in-flight frame, for ImGui_ImplVulkan_RenderDrawData()
// [Please zero-clear before use!]
struct ImGui_ImplVulkanH_FrameRenderBuffers
{
	VkDeviceMemory      VertexBufferMemory;
	VkDeviceMemory      IndexBufferMemory;
	VkDeviceSize        VertexBufferSize;
	VkDeviceSize        IndexBufferSize;
	VkBuffer            VertexBuffer;
	VkBuffer            IndexBuffer;
};


// TODO: Remove this - it's a copy from the ImGui vulkan implementation.
// Each viewport will hold 1 ImGui_ImplVulkanH_WindowRenderBuffers
// [Please zero-clear before use!]
struct ImGui_ImplVulkanH_WindowRenderBuffers
{
	uint32_t            Index;
	uint32_t            Count;
	ImGui_ImplVulkanH_FrameRenderBuffers* FrameRenderBuffers;
};


class ImGuiPipeline
{
public:
	ImGuiPipeline(std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain)
		:m_pDeviceContext {pDeviceContext}, m_pSwapchain {pSwapchain} {}

	// Disable copying.
	ImGuiPipeline() = default;
	ImGuiPipeline(const ImGuiPipeline&) = delete;
	ImGuiPipeline& operator=(const ImGuiPipeline&) = delete;

	void Init();

	void Recreate();

	void Destroy();

	uint32_t                 g_QueueFamily {(uint32_t)-1};
	VkQueue                  g_Queue {VK_NULL_HANDLE};
	VkDescriptorPool         g_DescriptorPool {VK_NULL_HANDLE};

	ImGui_ImplVulkanH_Window g_windowData {};
	bool                     g_SwapChainRebuild {false};

	VkAllocationCallbacks* g_Allocator {nullptr};
	VkPipelineCache          g_PipelineCache {VK_NULL_HANDLE};

	void FrameRender(ImGui_ImplVulkanH_Window* wd);

	void FramePresent(ImGui_ImplVulkanH_Window* wd);

	void ImGuiCreateOrResizeWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator);

	void CleanupVulkan();

	void CleanupVulkanWindow();

private:

	void Create();

	void ImGuiInitFontTexture(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext);

	int ImGuiGetMinImageCountFromPresentMode(VkPresentModeKHR present_mode);

	void ImGuiDestroyFrame(VkDevice device, ImGui_ImplVulkanH_Frame* fd, const VkAllocationCallbacks* allocator);

	void ImGuiDestroyFrameSemaphores(VkDevice device, ImGui_ImplVulkanH_FrameSemaphores* fsd, const VkAllocationCallbacks* allocator);

	void ImGuiDestroyWindow(VkInstance instance, VkDevice device, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator);

	void ImGuiDestroyFrameRenderBuffers(VkDevice device, ImGui_ImplVulkanH_FrameRenderBuffers* buffers, const VkAllocationCallbacks* allocator);

	void ImGuiDestroyWindowRenderBuffers(VkDevice device, ImGui_ImplVulkanH_WindowRenderBuffers* buffers, const VkAllocationCallbacks* allocator);

	void ImGuiCreateWindowSwapChain(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator);

	void ImGuiCreateWindowCommandBuffers(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator);

	void SetupVulkan(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext);

	void SetupVulkanWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, ImGui_ImplVulkanH_Window* wd);

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext {nullptr};

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain {nullptr};
};
}
