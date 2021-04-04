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
	const VkAllocationCallbacks* kPAllocator {nullptr};

	ImGuiPipeline(std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain)
		:m_pDeviceContext {pDeviceContext}, m_pSwapchain {pSwapchain} {}

	// Disable copying.
	ImGuiPipeline() = default;
	ImGuiPipeline(const ImGuiPipeline&) = delete;
	ImGuiPipeline& operator=(const ImGuiPipeline&) = delete;

	void Init();

	void Recreate();

	void Destroy();

	VkQueue                  g_Queue {VK_NULL_HANDLE};
	VkDescriptorPool         g_DescriptorPool {VK_NULL_HANDLE};

	bool                     g_SwapChainRebuild {false};

	VkPipelineCache          g_PipelineCache {VK_NULL_HANDLE};

	void CreateOrResizeWindow(uint32_t queue_family);

	void CleanupVulkan();

	void CleanupVulkanWindow();

	void Render();

	void Present();

private:

	void Create();

	void CreateRenderPass();

	void CreateFramebuffers();

	void CreateImageViews();

	void CreateFontsTexture();

	void CreateDescriptorPool();

	int GetMinImageCountFromPresentMode(VkPresentModeKHR present_mode);

	void DestroyFrame(ImGui_ImplVulkanH_Frame* fd);

	void DestroyFrameSemaphores(ImGui_ImplVulkanH_FrameSemaphores* fsd);

	void DestroyWindow();

	void DestroyFrameRenderBuffers(ImGui_ImplVulkanH_FrameRenderBuffers* buffers);

	void DestroyWindowRenderBuffers(ImGui_ImplVulkanH_WindowRenderBuffers* buffers);

	void CreateWindowSwapChain();

	void CreateWindowCommandBuffers();

	void SetupVulkan();

	void SetupVulkanWindow();

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext {nullptr};

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain {nullptr};

	ImGui_ImplVulkanH_Window m_windowData {};
};
}
