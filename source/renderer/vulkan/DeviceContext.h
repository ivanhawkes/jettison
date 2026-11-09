#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

#include "Window.h"


namespace Jettison::Renderer
{
// Inline, not static: this is a header, and a static function gives every translation
// unit that includes it its own copy, seven of which then report it as an unused function.
inline void check_vk_result(VkResult err)
{
	if (err == 0)
		return;
	fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
	if (err < 0)
		abort();
}


struct QueueFamilyIndices
{
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;

	bool isComplete()
	{
		return graphicsFamily.has_value() && presentFamily.has_value();
	}
};


struct SwapChainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities = {};
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};


class DeviceContext
{
  public:
	DeviceContext(std::shared_ptr<Window> pWindow) : m_pWindow{pWindow}
	{
	}

	// Disable copying.
	DeviceContext() = default;
	DeviceContext(const DeviceContext &) = delete;
	DeviceContext &operator=(const DeviceContext &) = delete;

	void Init();

	void Destroy();

	VkResult WaitIdle()
	{
		return vkDeviceWaitIdle(m_logicalDevice);
	}

	// Names a Vulkan object for the validation layer and debug tools. A validation message
	// about "VkBuffer 0x3" identifies a buffer to whoever wrote the code that made it, not to
	// anyone reading a bug report; a name says which of the engine's buffers it is and which
	// image slot. No-ops when VK_EXT_debug_utils is unavailable, which is every release build.
	void SetDebugName(VkBuffer object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkImage object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkImageView object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkFramebuffer object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_FRAMEBUFFER, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkPipeline object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkRenderPass object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_RENDER_PASS, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkPipelineLayout object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkDescriptorPool object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_POOL, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkDescriptorSet object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkCommandBuffer object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_COMMAND_BUFFER, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkSemaphore object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkFence object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_FENCE, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkSampler object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkDeviceMemory object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_DEVICE_MEMORY, reinterpret_cast<uint64_t>(object), name);
	}

	// Marks a labelled region of a command buffer, so a layer message raised while a command
	// is executing names the frame region it came from. No-ops without the extension.
	void BeginDebugLabel(VkCommandBuffer commandBuffer, const std::string &name) const;
	void EndDebugLabel(VkCommandBuffer commandBuffer) const;

	// Wait for the device to return sensible values for the window size after a resize event.
	void WaitOnWindowResized() const;

	inline VkPhysicalDevice GetPhysicalDevice() const
	{
		return m_physicalDevice;
	}

	inline VkDevice GetLogicalDevice() const
	{
		return m_logicalDevice;
	}

	inline VkInstance GetInstance() const
	{
		return m_instance;
	}

	inline VkSampleCountFlagBits GetMsaaSamples() const
	{
		return m_msaaSamples;
	}

	inline VkQueue GetGraphicsQueue() const
	{
		return m_graphicsQueue;
	}

	inline uint32_t GetGraphicsQueueIndex() const
	{
		return m_graphicsQueueIndex;
	}

	inline VkQueue GetPresentQueue() const
	{
		return m_presentQueue;
	}

	inline uint32_t GetPresentQueueIndex() const
	{
		return m_presentQueueIndex;
	}

	inline std::shared_ptr<Window> GetWindow() const
	{
		return m_pWindow;
	}

	// Utilities.

	VkFormat FindDepthFormat();

	void CreateBuffer(
	    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer,
	    VkDeviceMemory &bufferMemory);

	void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

	void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

	VkCommandBuffer BeginSingleTimeCommands();

	void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

	void TransitionImageLayout(
	    VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout,
	    uint32_t mipLevels);

	void CreateImage(
	    uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
	    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage,
	    VkMemoryPropertyFlags properties, VkImage &image, VkDeviceMemory &imageMemory);

	VkImageView CreateImageView(
	    VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

	VkShaderModule CreateShaderModule(const std::vector<char> &code);

	VkSurfaceKHR GetSurface() const
	{
		return m_surface;
	}

	VkCommandPool GetCommandPool() const
	{
		return m_commandPool;
	}

	SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device);

	// True when the debug messenger is live, i.e. validation layers and VK_EXT_debug_utils
	// are both in play, which is the debug build. Callers use it to make a run that has to be
	// silent (the headless smoke test) check that nothing reported anything.
	bool DebugUtilsActive() const
	{
		return m_debugUtilsActive;
	}

	QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice m_device);

	VkSurfaceFormatKHR FindSupportedSurfaceFormat(
	    const std::vector<VkFormat> &candidates, VkColorSpaceKHR requestedColourSpace);

	// Request a certain mode and confirm that it is available. If not use VK_PRESENT_MODE_FIFO_KHR
	// which is mandatory
	VkPresentModeKHR FindSupportedPresentMode(const std::vector<VkPresentModeKHR> candidates);

  private:
	bool CheckValidationLayerSupport();

	void CreateInstance();

	void CreateSurface();

	bool CheckDeviceExtensionSupport(VkPhysicalDevice device);

	bool IsDeviceSuitable(VkPhysicalDevice m_device);

	void PickPhysicalDevice();

	void CreateLogicalDevice();

	uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

	bool HasStencilComponent(VkFormat format);

	VkSampleCountFlagBits GetMaxUsableSampleCount();

	VkFormat FindSupportedFormat(
	    const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

	void CreateCommandPool();

	bool HasInstanceExtension(const char *name) const;

	// The messenger and the name-setter are resolved through the loader instead of linked:
	// VK_EXT_debug_utils predates this project's Vulkan 1.0 target, its entry points are only
	// exported by newer loaders, and a build that cannot find them has to carry on without
	// names, not fail to link. The messenger is created through the instance's pNext, so it
	// reports during instance and device creation, and the loader destroys it with the
	// instance.
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
	    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT types,
	    const VkDebugUtilsMessengerCallbackDataEXT *pData, void *pUserData);

	void SetObjectName(VkObjectType type, uint64_t handle, const std::string &name) const;

	// A pointer to the window with which this device context is associated.
	std::shared_ptr<Window> m_pWindow{nullptr};

	// An instance of Vulkan.
	VkInstance m_instance{VK_NULL_HANDLE};

	// A physical device / GPU with Vulkan support.
	VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};

	// A logical device which allows you to access the physical device through a defined API.
	VkDevice m_logicalDevice{VK_NULL_HANDLE};

	// A Vulkan handle to the graphics queue.
	VkQueue m_graphicsQueue{VK_NULL_HANDLE};

	// The numeric index of the graphics queue.
	uint32_t m_graphicsQueueIndex{std::numeric_limits<uint32_t>::max()};

	// A Vulkan handle to the presentation queue.
	VkQueue m_presentQueue{VK_NULL_HANDLE};

	// The numeric index of the presentation queue.
	uint32_t m_presentQueueIndex{std::numeric_limits<uint32_t>::max()};

	VkSampleCountFlagBits m_msaaSamples{VK_SAMPLE_COUNT_1_BIT};

	VkSurfaceKHR m_surface{VK_NULL_HANDLE};

	VkCommandPool m_commandPool{VK_NULL_HANDLE};

	// The name-setter is resolved through the loader instead of linked: VK_EXT_debug_utils
	// predates this project's Vulkan 1.0 target and its entry points are only exported by
	// newer loaders, so a build on an older one has to carry on without names, not fail to
	// link. The messenger needs no handle and no pointer of its own: it arrives through the
	// instance's pNext chain, which reports during instance and device creation, and the
	// loader destroys it with the instance.
	PFN_vkSetDebugUtilsObjectNameEXT m_pfnSetDebugUtilsObjectName{nullptr};
	PFN_vkCmdBeginDebugUtilsLabelEXT m_pfnCmdBeginDebugLabel{nullptr};
	PFN_vkCmdEndDebugUtilsLabelEXT m_pfnCmdEndDebugLabel{nullptr};
	bool m_debugUtilsActive{false};
};
} // namespace Jettison::Renderer
