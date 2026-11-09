#pragma once

#include <vulkan/vulkan.h>

#include <filesystem>
#include <memory>
#include <utility>

#include "../DeviceContext.h"

struct GLFWwindow;


namespace Jettison::Renderer
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

	void Init(GLFWwindow *window, VkRenderPass renderPass, uint32_t imageCount);
	void InitVulkanBackend(VkRenderPass renderPass, uint32_t imageCount);
	void ShutdownVulkanBackend();
	void Destroy();

	void BeginFrame();
	void ShowDemoWindow(bool *show);
	void EndFrame();
	void RenderDrawData(VkCommandBuffer commandBuffer) const;

  private:
	void CreateDescriptorPool();

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	std::filesystem::path m_assetDirectory;
	VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
	bool m_contextCreated{false};
	bool m_glfwBackendInitialized{false};
	bool m_vulkanBackendInitialized{false};
};

} // namespace Jettison::Renderer