#pragma once

#include <vulkan/vulkan.h>

#include "DeviceContext.h"

// STD.
#include <string>


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

	inline VkExtent2D GetExtents() const { return m_swapchainExtent; }

	inline uint32_t GetImageCount() const { return m_swapchainImageCount; }

	inline uint32_t* GetImageCountAddress() { return &m_swapchainImageCount; }

	inline VkFormat GetImageFormat() const { return m_swapchainImageFormat; }

private:
	void Create();

	VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);

	VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext;

	QueueFamilyIndices m_indicies;

	VkSwapchainKHR m_vkSwapchainHandle {VK_NULL_HANDLE};

	uint32_t m_swapchainImageCount {0};
	VkFormat m_swapchainImageFormat {VK_FORMAT_UNDEFINED};
	VkExtent2D m_swapchainExtent {0, 0};
};
}
