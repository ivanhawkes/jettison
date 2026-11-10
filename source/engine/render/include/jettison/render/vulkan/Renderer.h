#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <cstdint>
#include <optional>
#include <vector>

#include <jettison/render/Scene.h>
#include <jettison/render/SceneSystems.h>
#include <jettison/render/vulkan/DeviceContext.h>
#include <jettison/render/vulkan/Pipeline.h>
#include <jettison/render/vulkan/Swapchain.h>
#include <jettison/render/vulkan/Window.h>
#include <jettison/render/vulkan/imgui/ImGuiRenderer.h>


namespace Jettison::Render
{
class Renderer
{
  public:
	Renderer(
	    std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Window> pWindow,
	    std::shared_ptr<Jettison::Render::Swapchain> pSwapchain,
	    std::shared_ptr<Jettison::Render::Pipeline> pPipeline)
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

	// The present half of the frame: the image the current slot acquired, the render-finished
	// semaphore of that image and the swapchain answer that follows a submitted frame. Render
	// ends by calling it; a loop that records several views into one image calls it after the
	// last one it submitted. The answer is Render's answer: this frame recreated the swapchain.
	bool Present(ImGuiRenderer &imguiRenderer);

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
	std::shared_ptr<Jettison::Render::Swapchain> m_pSwapchain{nullptr};

	// Pipeline.
	std::shared_ptr<Jettison::Render::Pipeline> m_pPipeline{nullptr};

	std::vector<VkSemaphore> m_imageAvailableSemaphores{};
	std::vector<VkSemaphore> m_renderFinishedSemaphores{};

	size_t m_currentFrame{0};
	// The swapchain image the current frame slot acquired. Render's acquire writes it and Present
	// reads it, one frame at a time.
	uint32_t m_presentImageIndex{0};
	std::vector<VkFence> m_imagesInFlight{};
	std::vector<VkFence> m_inFlightFences{};

	// The scene generation the model resources currently cached were built from. Empty until
	// the first frame, so a scene loaded before Init() costs nothing.
	std::optional<std::uint64_t> m_sceneGeneration{};

	std::shared_ptr<Window> m_pWindow{nullptr};
};
} // namespace Jettison::Render
