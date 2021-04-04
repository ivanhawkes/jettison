#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <string>

#include "DeviceContext.h"


namespace Jettison::Renderer
{
class Swapchain
{
public:
	Swapchain(std::shared_ptr<DeviceContext> pDeviceContext)
		:m_pDeviceContext {pDeviceContext} {}

	// Disable copying.
	Swapchain() = default;
	Swapchain(const Swapchain&) = delete;
	Swapchain& operator=(const Swapchain&) = delete;

	void Init();

	void Recreate();

	void Destroy();

	inline VkSwapchainKHR GetVkSwapchainHandle() const { return m_vkSwapchainHandle; }

	inline VkExtent2D GetExtents() const { return m_extents; }

	inline uint32_t GetMinImageCount() const { return m_minImageCount; }

	inline uint32_t GetMaxImageCount() const { return m_maxImageCount; }

	inline uint32_t GetImageCount() const { return m_imageCount; }

	inline uint32_t* GetImageCountAddress() { return &m_imageCount; }

	inline VkFormat GetImageFormat() const { return m_imageFormat; }

	inline bool IsMinimised() const { return m_extents.height <= 0 || m_extents.width <= 0; }

	inline bool IsWindowSizeValid() const { return m_extents.height > 0 && m_extents.width > 0; }

private:
	void Create();

	VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

	VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext;

	QueueFamilyIndices m_indicies;

	VkSwapchainKHR m_vkSwapchainHandle {VK_NULL_HANDLE};

	uint32_t m_minImageCount {0};
	uint32_t m_maxImageCount {0};
	uint32_t m_imageCount {0};
	VkFormat m_imageFormat {VK_FORMAT_UNDEFINED};
	VkExtent2D m_extents {0, 0};
};
}
