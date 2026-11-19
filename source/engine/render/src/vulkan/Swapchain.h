#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

#include "vulkan/DeviceContext.h"


namespace Jettison::Render
{
class Swapchain
{
  public:
	Swapchain(std::shared_ptr<DeviceContext> pDeviceContext) : m_pDeviceContext{pDeviceContext}
	{
	}

	Swapchain() = default;

	Swapchain(const Swapchain &) = delete;

	Swapchain &operator=(const Swapchain &) = delete;

	void Init();

	void Recreate();

	void Destroy();

	inline VkSwapchainKHR GetVkSwapchainHandle() const
	{
		return m_vkSwapchainHandle;
	}

	inline VkExtent2D GetExtents() const
	{
		return m_extents;
	}

	inline uint32_t GetMinImageCount() const
	{
		return m_minImageCount;
	}

	inline uint32_t GetMaxImageCount() const
	{
		return m_maxImageCount;
	}

	inline uint32_t GetImageCount() const
	{
		return m_imageCount;
	}

	inline uint32_t *GetImageCountAddress()
	{
		return &m_imageCount;
	}

	inline VkFormat GetImageFormat() const
	{
		return m_imageFormat;
	}

	// Whether writing linear values to the images stores them sRGB-encoded, which an *_SRGB
	// format does in hardware. When the surface offered only UNORM formats in the sRGB colour
	// space, the output pass encodes instead.
	bool EncodesSrgb() const
	{
		return m_encodesSrgb;
	}

	// Whether the images can be copied from, which a screenshot needs (render/Screenshot.h). Every
	// surface measured offers it; one that does not leaves the screenshot pass out of the plan.
	bool CanReadBack() const
	{
		return m_canReadBack;
	}

	VkImage GetImage(uint32_t index) const
	{
		return m_images.at(index);
	}

	VkImageView GetImageView(uint32_t index) const
	{
		return m_imageViews.at(index);
	}

	// Without a surface (Window::Init's offscreen), the images are the swapchain's own, drawn into
	// in turn and never presented: there is no VkSwapchainKHR, and nothing to acquire.
	bool IsOffscreen() const
	{
		return m_pDeviceContext->GetSurface() == VK_NULL_HANDLE;
	}

	// The offscreen image the next frame draws into, the one after the last. A frame slot waits on
	// the fence of the frame that last drew into it, as it does on a swapchain image.
	uint32_t NextOffscreenImage()
	{
		m_nextImage = (m_nextImage + 1) % m_imageCount;
		return m_nextImage;
	}

	inline bool IsMinimised() const
	{
		return m_extents.height <= 0 || m_extents.width <= 0;
	}

	inline bool IsWindowSizeValid() const
	{
		return m_extents.height > 0 && m_extents.width > 0;
	}

  private:
	void Create();

	void CreateOffscreen();

	VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &availableFormats);

	VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR> &availablePresentModes);

	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities);

	std::shared_ptr<DeviceContext> m_pDeviceContext;

	QueueFamilyIndices m_indicies;

	VkSwapchainKHR m_vkSwapchainHandle{VK_NULL_HANDLE};

	uint32_t m_minImageCount{0};
	uint32_t m_maxImageCount{0};
	uint32_t m_imageCount{0};
	VkFormat m_imageFormat{VK_FORMAT_UNDEFINED};
	bool m_encodesSrgb{false};
	bool m_canReadBack{false};
	VkExtent2D m_extents{0, 0};
	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_imageViews;
	// The images when offscreen, which m_images then lists the handles of.
	std::vector<Image> m_offscreenImages;
	uint32_t m_nextImage{0};
};
} // namespace Jettison::Render
