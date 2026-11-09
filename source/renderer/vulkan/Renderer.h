#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <cstdint>
#include <optional>
#include <vector>

#include "DeviceContext.h"
#include "Pipeline.h"
#include "Scene.h"
#include "SceneSystems.h"
#include "Swapchain.h"
#include "Window.h"
#include "imgui/ImGuiRenderer.h"


namespace Jettison::Renderer
{
class Renderer
{
  public:
	Renderer(
	    std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Window> pWindow,
	    std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	    std::shared_ptr<Jettison::Renderer::Pipeline> pPipeline)
	    : m_pDeviceContext{pDeviceContext}, m_pSwapchain{pSwapchain}, m_pPipeline{pPipeline},
	      m_pWindow{pWindow}
	{
	}

	// Disable copying.
	Renderer() = default;
	Renderer(const Renderer &) = delete;
	Renderer &operator=(const Renderer &) = delete;

	void Init();

	void Destroy();

	bool Render(
	    Scene &scene, const SceneUpdateSystem &updateSystem, const SceneRenderSystem &renderSystem,
	    float deltaTime, ImGuiRenderer &imguiRenderer);

	void Present();

  private:
	void InitVulkan();

	void CreateSyncObjects();

	void CreateRenderFinishedSemaphores();

	void RecreateRenderFinishedSemaphores();

	void RecreateSwapchain(ImGuiRenderer &imguiRenderer);

	void UpdateUniformBuffer(uint32_t currentImage, const entt::registry &sceneRegistry);

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext{nullptr};

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain{nullptr};

	// Pipeline.
	std::shared_ptr<Jettison::Renderer::Pipeline> m_pPipeline{nullptr};

	std::vector<VkSemaphore> m_imageAvailableSemaphores{};
	std::vector<VkSemaphore> m_renderFinishedSemaphores{};

	size_t m_currentFrame{0};
	std::vector<VkFence> m_imagesInFlight{};
	std::vector<VkFence> m_inFlightFences{};

	// The scene generation the model resources currently cached were built from. Empty until
	// the first frame, so a scene loaded before Init() costs nothing.
	std::optional<std::uint64_t> m_sceneGeneration{};

	std::shared_ptr<Window> m_pWindow{nullptr};
};
} // namespace Jettison::Renderer
