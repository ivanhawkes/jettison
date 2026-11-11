#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <utility>

#include "vulkan/DeviceContext.h"

struct GLFWwindow;


namespace Jettison::Render
{
class ImGuiRenderer
{
  public:
	ImGuiRenderer(std::shared_ptr<DeviceContext> pDeviceContext, std::filesystem::path assetDirectory)
	    : m_pDeviceContext{std::move(pDeviceContext)}, m_assetDirectory{std::move(assetDirectory)}
	{
	}

	ImGuiRenderer() = default;
	ImGuiRenderer(const ImGuiRenderer &) = delete;
	ImGuiRenderer &operator=(const ImGuiRenderer &) = delete;

	// The UI is drawn with dynamic rendering into one single-sampled attachment of colorFormat,
	// which is the output pass's: nothing about it depends on the swapchain's size or image count,
	// so a resize leaves the backend alone. frameCount is how many frames may be in flight, which
	// is how many sets of vertex buffers the backend cycles through.
	void Init(GLFWwindow *window, VkFormat colorFormat, uint32_t frameCount);
	void ShutdownVulkanBackend();
	void Destroy();

	void BeginFrame();
	void EndFrame();
	void RenderDrawData(VkCommandBuffer commandBuffer) const;

  private:
	void CreateDescriptorPool();
	void InitVulkanBackend(VkFormat colorFormat, uint32_t frameCount);

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	std::filesystem::path m_assetDirectory;
	VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
	bool m_contextCreated{false};
	bool m_glfwBackendInitialized{false};
	bool m_vulkanBackendInitialized{false};
};

} // namespace Jettison::Render