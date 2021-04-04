#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <array>
#include <stdio.h>

#include "DeviceContext.h"
#include "Pipeline.h"
#include "Swapchain.h"
#include "Window.h"


namespace Jettison::Renderer
{
class Renderer {
public:
	Renderer(std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Window> pWindow,
		std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, std::shared_ptr<Jettison::Renderer::Pipeline> pPipeline)
		:m_pDeviceContext {pDeviceContext}, m_pWindow {pWindow}, m_pSwapchain {pSwapchain}, m_pPipeline {pPipeline}{}

	// Disable copying.
	Renderer() = default;
	Renderer(const Renderer&) = delete;
	Renderer& operator=(const Renderer&) = delete;

	void Init();

	void Destroy();

	void Render();

	void Present();

private:
	void InitVulkan();

	void CreateSyncObjects();

	void UpdateUniformBuffer(uint32_t currentImage);

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext {nullptr};

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain {nullptr};

	// Pipeline.
	std::shared_ptr<Jettison::Renderer::Pipeline> m_pPipeline {nullptr};

	VkSampleCountFlagBits m_msaaSamples {VK_SAMPLE_COUNT_1_BIT};

	std::vector<VkSemaphore> m_imageAvailableSemaphores {};
	std::vector<VkSemaphore> m_renderFinishedSemaphores {};

	size_t m_currentFrame {0};
	std::vector<VkFence> m_imagesInFlight {};
	std::vector<VkFence> m_inFlightFences {};

	std::shared_ptr<Window> m_pWindow {nullptr};

	bool m_show_demo_window {true};
};
}
