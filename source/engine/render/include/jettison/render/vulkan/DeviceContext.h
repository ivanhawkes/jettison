#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jettison/render/vulkan/Window.h>

// The Vulkan Memory Allocator's handles, declared as it declares them (VK_DEFINE_HANDLE), so this
// header does not carry vk_mem_alloc.h to everything that includes it.
struct VmaAllocator_T;
struct VmaAllocation_T;


namespace Jettison::Render
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


// Where an allocation lives. Device memory is for what the GPU reads and writes; upload memory is
// host-visible, mapped for the allocation's lifetime and written front to back by the CPU, which
// is what per-frame data and staging copies do. VMA picks the memory type for each.
enum class MemoryUse
{
	Device,
	Upload
};

// A buffer and the allocation it is bound to. Empty when the handle is null; DestroyBuffer empties
// it. An upload buffer is mapped from creation to destruction.
struct Buffer
{
	VkBuffer handle{VK_NULL_HANDLE};
	VmaAllocation_T *allocation{nullptr};
	VkDeviceSize size{0};
	void *mapped{nullptr};
};

struct Image
{
	VkImage handle{VK_NULL_HANDLE};
	VmaAllocation_T *allocation{nullptr};
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
	// The Vulkan version the instance asks for, and the one VMA and ImGui are told about. 1.3 for
	// dynamic rendering, which the declared pass list records passes with instead of render pass
	// objects, and for synchronization2, which its barriers are written in. Both are core there,
	// and lavapipe, where the gate runs, has both.
	static constexpr uint32_t kApiVersion = VK_API_VERSION_1_3;

	// The most samples the HDR target is given. Every device supports four for colour and depth
	// attachments (the spec's required minimum), and a 1080p RGBA16F target at eight costs 66 MB
	// more than at four, for an edge quality the resolve barely shows.
	static constexpr VkSampleCountFlagBits kMaxMsaaSamples = VK_SAMPLE_COUNT_4_BIT;

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
		return m_logicalDevice != VK_NULL_HANDLE ? vkDeviceWaitIdle(m_logicalDevice) : VK_SUCCESS;
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
	void SetDebugName(VkShaderModule object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(object), name);
	}
	void SetDebugName(VkDescriptorSetLayout object, const std::string &name) const
	{
		SetObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(object), name);
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

	// Whether BC textures were enabled on the device, with BC7 sampled and filtered.
	inline bool HasBlockCompression() const
	{
		return m_blockCompression;
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

	// Every buffer and image the engine allocates goes through these, and through VMA: one
	// allocator per device, sub-allocating from large blocks where vkAllocateMemory would make
	// each its own allocation against a driver limit of as few as 4096. The name goes to the
	// object and to its allocation, so a leak reported at teardown says what leaked.
	Buffer CreateBuffer(
	    VkDeviceSize size, VkBufferUsageFlags usage, MemoryUse memory, const std::string &name);
	void DestroyBuffer(Buffer &buffer);
	// Copies into an upload buffer and flushes the range, which memory that is not host-coherent
	// needs before the device may read it; on coherent memory the flush does nothing.
	void WriteBuffer(
	    const Buffer &buffer, const void *data, VkDeviceSize size, VkDeviceSize offset = 0);
	// A device-local buffer holding the bytes given, filled through a staging buffer that is gone
	// by the time this returns.
	Buffer CreateDeviceBuffer(
	    const void *data, VkDeviceSize size, VkBufferUsageFlags usage, const std::string &name);

	void CopyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

	void CopyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);

	VkCommandBuffer BeginSingleTimeCommands();

	void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

	void TransitionImageLayout(
	    VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout,
	    uint32_t mipLevels);

	// A device-local 2D image. Attachments get memory of their own (VMA's dedicated allocation),
	// as drivers prefer for render targets.
	Image CreateImage(
	    uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
	    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const std::string &name);
	// A device-local cube map: six square layers, in Vulkan's face order (+X, -X, +Y, -Y, +Z, -Z).
	Image CreateCubeImage(
	    uint32_t size, uint32_t mipLevels, VkFormat format, VkImageUsageFlags usage,
	    const std::string &name);
	void DestroyImage(Image &image);

	VkImageView CreateImageView(
	    VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
	// A view of any part of an image: a cube, or one level of one face to render into.
	VkImageView CreateImageView(
	    VkImage image, VkFormat format, VkImageViewType viewType, const VkImageSubresourceRange &range);

	VkShaderModule CreateShaderModule(std::span<const std::byte> code, const std::string &name);

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

	void CreateAllocator();
	// Reports every allocation still live, as a validation error would be reported, then
	// destroys the allocator: a leak fails the headless render test, which reads that stream.
	void DestroyAllocator();

	bool HasStencilComponent(VkFormat format);

	VkSampleCountFlagBits GetMaxUsableSampleCount();

	VkFormat FindSupportedFormat(
	    const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

	void CreateCommandPool();

	// Whether the loader (or, given one, the layer) offers the instance extension.
	bool HasInstanceExtension(const char *name, const char *layer = nullptr) const;

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
	bool m_blockCompression{false};

	VkSurfaceKHR m_surface{VK_NULL_HANDLE};

	VkCommandPool m_commandPool{VK_NULL_HANDLE};

	VmaAllocator_T *m_allocator{nullptr};

	VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};
	PFN_vkDestroyDebugUtilsMessengerEXT m_pfnDestroyDebugUtilsMessenger{nullptr};
	PFN_vkSetDebugUtilsObjectNameEXT m_pfnSetDebugUtilsObjectName{nullptr};
	PFN_vkCmdBeginDebugUtilsLabelEXT m_pfnCmdBeginDebugLabel{nullptr};
	PFN_vkCmdEndDebugUtilsLabelEXT m_pfnCmdEndDebugLabel{nullptr};
	bool m_debugUtilsActive{false};
};
} // namespace Jettison::Render
