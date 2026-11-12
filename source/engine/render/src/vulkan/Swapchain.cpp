#include "vulkan/Swapchain.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

// STD.
#include <array>
#include <cmath>
#include <cstring>
#include <set>
#include <stdexcept>
#include <string>


namespace Jettison::Render
{
void Swapchain::Init()
{
	Create();
}


void Swapchain::Create()
{
	SwapChainSupportDetails swapChainSupport =
	    m_pDeviceContext->QuerySwapChainSupport(m_pDeviceContext->GetPhysicalDevice());

	VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode = ChooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent = ChooseSwapExtent(swapChainSupport.capabilities);

	// A surface that is not mapped reports no extent, which is what a minimized window looks
	// like to the loader on Wayland, and vkCreateSwapchainKHR requires width and height of at
	// least 1 (VUID-VkSwapchainCreateInfoKHR-imageExtent-01274). The recreate path is guarded by
	// DeviceContext::WaitOnWindowResized, which parks until the framebuffer has a size; the
	// first create has no such guard, so a window that starts out minimized reaches here. The
	// clamp keeps the create legal, and the frame loop presents to a 1x1 swapchain until the
	// compositor gives the surface a size, which is what the projection division expects too.
	extent.width = std::max(extent.width, 1u);
	extent.height = std::max(extent.height, 1u);

	// Get a reasonable amount of swapchain images.
	m_minImageCount = swapChainSupport.capabilities.minImageCount;
	m_maxImageCount = swapChainSupport.capabilities.maxImageCount;
	m_imageCount = m_minImageCount + 1;
	if (m_maxImageCount > 0)
		m_imageCount = std::min(m_imageCount, m_maxImageCount);

	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = m_pDeviceContext->GetSurface();
	createInfo.minImageCount = m_imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	m_canReadBack =
	    (swapChainSupport.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
	if (m_canReadBack)
		createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

	QueueFamilyIndices indices =
	    m_pDeviceContext->FindQueueFamilies(m_pDeviceContext->GetPhysicalDevice());
	uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};

	if (indices.graphicsFamily != indices.presentFamily)
	{
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	}
	else
	{
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;

	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;

	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if (vkCreateSwapchainKHR(
	        m_pDeviceContext->GetLogicalDevice(), &createInfo, nullptr, &m_vkSwapchainHandle) !=
	    VK_SUCCESS)
	{
		throw std::runtime_error("failed to create swap chain");
	}
	if (vkGetSwapchainImagesKHR(
	        m_pDeviceContext->GetLogicalDevice(), m_vkSwapchainHandle, &m_imageCount, nullptr) !=
	    VK_SUCCESS)
	{
		throw std::runtime_error("failed to query swap chain images");
	}
	m_images.resize(m_imageCount);
	if (vkGetSwapchainImagesKHR(
	        m_pDeviceContext->GetLogicalDevice(), m_vkSwapchainHandle, &m_imageCount,
	        m_images.data()) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to query swap chain images");
	}

	m_imageFormat = surfaceFormat.format;
	m_encodesSrgb = surfaceFormat.format == VK_FORMAT_B8G8R8A8_SRGB ||
	                surfaceFormat.format == VK_FORMAT_R8G8B8A8_SRGB;
	m_extents = extent;

	m_imageViews.resize(m_imageCount);
	for (uint32_t i = 0; i < m_imageCount; ++i)
	{
		m_imageViews[i] =
		    m_pDeviceContext->CreateImageView(m_images[i], m_imageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
		m_pDeviceContext->SetDebugName(m_images[i], "swapchain-image-" + std::to_string(i));
		m_pDeviceContext->SetDebugName(m_imageViews[i], "swapchain-view-" + std::to_string(i));
	}
}


void Swapchain::Recreate()
{
	m_pDeviceContext->WaitOnWindowResized();

	Destroy();
	Create();
}


void Swapchain::Destroy()
{
	for (const VkImageView view : m_imageViews)
		vkDestroyImageView(m_pDeviceContext->GetLogicalDevice(), view, nullptr);
	m_imageViews.clear();
	m_images.clear();
	vkDestroySwapchainKHR(m_pDeviceContext->GetLogicalDevice(), m_vkSwapchainHandle, nullptr);
	m_vkSwapchainHandle = VK_NULL_HANDLE;
}


VkSurfaceFormatKHR Swapchain::ChooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &availableFormats)
{
	// The pipeline is linear up to the swapchain, so the swapchain should encode: an sRGB format
	// in the sRGB colour space first, whatever order the surface lists them in, then a UNORM one
	// in that colour space, which the output pass encodes for (EncodesSrgb).
	for (const VkFormat wanted :
	     {VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM,
	      VK_FORMAT_R8G8B8A8_UNORM})
		for (const auto &availableFormat : availableFormats)
			if (availableFormat.format == wanted &&
			    availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				return availableFormat;

	return availableFormats[0];
}


VkPresentModeKHR Swapchain::ChooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> &availablePresentModes)
{
	for (const VkPresentModeKHR &availablePresentMode : availablePresentModes)
	{
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			return availablePresentMode;
		}
	}

	return VK_PRESENT_MODE_FIFO_KHR;
}


VkExtent2D Swapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities)
{
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		return capabilities.currentExtent;
	}
	else
	{
		int width;
		int height;

		glfwGetFramebufferSize(m_pDeviceContext->GetWindow()->GetGLFWWindow(), &width, &height);

		VkExtent2D actualExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

		actualExtent.width = std::max(
		    capabilities.minImageExtent.width,
		    std::min(capabilities.maxImageExtent.width, actualExtent.width));
		actualExtent.height = std::max(
		    capabilities.minImageExtent.height,
		    std::min(capabilities.maxImageExtent.height, actualExtent.height));

		return actualExtent;
	}
}
} // namespace Jettison::Render
