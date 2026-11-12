#include <jettison/render/IRenderer.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vulkan/DeviceContext.h"
#include "vulkan/Renderer.h"
#include "vulkan/Swapchain.h"
#include "vulkan/Window.h"
#include "vulkan/imgui/ImGuiRenderer.h"

namespace Jettison::Render
{
namespace
{
// The backend's objects, in the order Init makes them: the window carries the surface, the device
// context the device the rest allocate on, and the renderer the output format the UI draws into.
class VulkanRenderer final : public IRenderer
{
  public:
	VulkanRenderer(
	    const Vfs::FileSystem &assets, Resource::ResourceManager &resources, RendererOptions options)
	    : m_assets{assets}, m_resources{resources}, m_options{std::move(options)}
	{
	}

	VulkanRenderer(const VulkanRenderer &) = delete;
	VulkanRenderer &operator=(const VulkanRenderer &) = delete;

	void Init() override
	{
		m_pWindow = std::make_shared<Window>();
		m_pWindow->Init();

		m_pDeviceContext = std::make_shared<DeviceContext>(m_pWindow);
		m_pDeviceContext->SetRequestedDevice(m_options.gpu);
		m_pDeviceContext->Init();
		if (m_options.requireDebugMessenger && !m_pDeviceContext->DebugUtilsActive())
			throw std::runtime_error("Vulkan debug messenger is required for the smoke test");
		m_pSwapchain = std::make_shared<Swapchain>(m_pDeviceContext);
		m_pSwapchain->Init();

		m_pRenderer = std::make_unique<Renderer>(
		    m_pDeviceContext, m_pWindow, m_pSwapchain, m_assets, m_resources);
		m_pRenderer->SetClusterCheck(m_options.clusterCheck);
		m_pRenderer->SetDepthPrepass(m_options.depthPrepass);
		m_pRenderer->Init();

		m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(m_pDeviceContext, m_options.assetDirectory);
		m_pImGuiRenderer->Init(
		    m_pWindow->GetGLFWWindow(), m_pRenderer->GetOutputFormat(), Renderer::kFramesInFlight);
	}

	void Destroy() override
	{
		// The frame loop's last frame is submitted, not waited on: Renderer::Render returns once
		// the work is in the queue, so this wait is its last fence, and nothing below is destroyed
		// while a command buffer that references it is in flight.
		if (m_pDeviceContext)
			check_vk_result(m_pDeviceContext->WaitIdle());
		if (m_pImGuiRenderer)
			m_pImGuiRenderer->Destroy();
		if (m_pRenderer)
			m_pRenderer->Destroy();
		if (m_pSwapchain)
			m_pSwapchain->Destroy();
		if (m_pDeviceContext)
			m_pDeviceContext->Destroy();
		if (m_pWindow)
			m_pWindow->Destroy();
		// The references go after the calls: nothing in the backend releases a GPU object in a
		// destructor, so the order of the resets carries nothing.
		m_pImGuiRenderer.reset();
		m_pRenderer.reset();
		m_pSwapchain.reset();
		m_pDeviceContext.reset();
		m_pWindow.reset();
	}

	GLFWwindow *GetGLFWWindow() const override
	{
		return m_pWindow ? m_pWindow->GetGLFWWindow() : nullptr;
	}

	void BeginUi() override
	{
		m_pImGuiRenderer->BeginFrame();
	}
	void EndUi() override
	{
		m_pImGuiRenderer->EndFrame();
	}

	bool Render(
	    const Scene::Scene &scene, const DebugDraw &debugDraw, const Overlay &overlay,
	    double presentationTime) override
	{
		return m_pRenderer->Render(scene, debugDraw, overlay, *m_pImGuiRenderer, presentationTime);
	}

	void SetExposure(float exposure) override
	{
		m_pRenderer->SetExposure(exposure);
	}
	void SetFade(const glm::vec3 &color, float amount) override
	{
		m_pRenderer->SetFade(color, std::clamp(amount, 0.0f, 1.0f));
	}
	void SetDebugView(DebugView view) override
	{
		m_pRenderer->SetDebugView(view);
	}
	DebugView GetDebugView() const override
	{
		return m_pRenderer->GetDebugView();
	}
	void SetDepthPrepass(bool enabled) override
	{
		m_pRenderer->SetDepthPrepass(enabled);
	}
	bool GetDepthPrepass() const override
	{
		return m_pRenderer->GetDepthPrepass();
	}

	const FrameStats &GetFrameStats() const override
	{
		return m_pRenderer->GetFrameStats();
	}
	const std::vector<PassTiming> &GetPassTimings() const override
	{
		return m_pRenderer->GetPassTimings();
	}
	std::uint64_t GetMeasuredFrames() const override
	{
		return m_pRenderer->GetMeasuredFrames();
	}
	void ResetPassTimings() override
	{
		m_pRenderer->ResetPassTimings();
	}
	const ClusterCheckTotals &GetClusterChecks() const override
	{
		return m_pRenderer->GetClusterChecks();
	}

	const std::string &DeviceName() const override
	{
		return m_pDeviceContext->GetDeviceName();
	}
	std::uint32_t FrameWidth() const override
	{
		return m_pSwapchain->GetExtents().width;
	}
	std::uint32_t FrameHeight() const override
	{
		return m_pSwapchain->GetExtents().height;
	}

  private:
	const Vfs::FileSystem &m_assets;
	Resource::ResourceManager &m_resources;
	RendererOptions m_options;

	// Shared where the backend hands one object to several of its neighbours: the device context to
	// all of them, the window to the device context and the renderer.
	std::shared_ptr<Window> m_pWindow;
	std::shared_ptr<DeviceContext> m_pDeviceContext;
	std::shared_ptr<Swapchain> m_pSwapchain;
	std::unique_ptr<Renderer> m_pRenderer;
	std::unique_ptr<ImGuiRenderer> m_pImGuiRenderer;
};
} // namespace

std::unique_ptr<IRenderer> CreateRenderer(
    const Vfs::FileSystem &assets, Resource::ResourceManager &resources, RendererOptions options)
{
	return std::make_unique<VulkanRenderer>(assets, resources, std::move(options));
}
} // namespace Jettison::Render
