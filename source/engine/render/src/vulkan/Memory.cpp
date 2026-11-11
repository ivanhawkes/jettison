// DeviceContext's memory: the Vulkan Memory Allocator, and the buffers and images made through it.
// The one translation unit that compiles VMA's implementation.
#include <jettison/render/vulkan/DeviceContext.h>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace Jettison::Render
{
void DeviceContext::CreateAllocator()
{
	VmaAllocatorCreateInfo createInfo{};
	createInfo.vulkanApiVersion = kApiVersion;
	createInfo.instance = m_instance;
	createInfo.physicalDevice = m_physicalDevice;
	createInfo.device = m_logicalDevice;
	if (vmaCreateAllocator(&createInfo, &m_allocator) != VK_SUCCESS)
		throw std::runtime_error("failed to create the Vulkan Memory Allocator");
}


void DeviceContext::DestroyAllocator()
{
	if (m_allocator == nullptr)
		return;
	VmaTotalStatistics statistics{};
	vmaCalculateStatistics(m_allocator, &statistics);
	if (const auto live = statistics.total.statistics.allocationCount; live != 0)
	{
		// The detailed map lists each live allocation with the name CreateBuffer or CreateImage
		// gave it. VMA asserts on destroying an allocator with allocations left, which ends a
		// Debug run here, so the report is written first, where the render test reads it.
		char *map = nullptr;
		vmaBuildStatsString(m_allocator, &map, VK_TRUE);
		std::fprintf(
		    stderr, "[vulkan:error] VMA: %u allocation(s) still live at teardown: %s\n", live, map);
		vmaFreeStatsString(m_allocator, map);
	}
	vmaDestroyAllocator(m_allocator);
	m_allocator = nullptr;
}


Buffer DeviceContext::CreateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, MemoryUse memory, const std::string &name)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocationInfo{};
	allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
	if (memory == MemoryUse::Upload)
		allocationInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
	else if (memory == MemoryUse::Readback)
		allocationInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	Buffer buffer;
	VmaAllocationInfo allocated{};
	if (vmaCreateBuffer(
	        m_allocator, &bufferInfo, &allocationInfo, &buffer.handle, &buffer.allocation,
	        &allocated) != VK_SUCCESS)
		throw std::runtime_error("failed to create buffer '" + name + "'");
	buffer.size = size;
	buffer.mapped = allocated.pMappedData;
	vmaSetAllocationName(m_allocator, buffer.allocation, name.c_str());
	SetDebugName(buffer.handle, name);
	return buffer;
}


void DeviceContext::DestroyBuffer(Buffer &buffer)
{
	if (buffer.handle != VK_NULL_HANDLE)
		vmaDestroyBuffer(m_allocator, buffer.handle, buffer.allocation);
	buffer = {};
}


void DeviceContext::WriteBuffer(
    const Buffer &buffer, const void *data, VkDeviceSize size, VkDeviceSize offset)
{
	if (buffer.mapped == nullptr)
		throw std::logic_error("WriteBuffer: the buffer is not an upload buffer");
	if (offset + size > buffer.size)
		throw std::logic_error("WriteBuffer: the write runs past the end of the buffer");
	std::memcpy(static_cast<std::byte *>(buffer.mapped) + offset, data, static_cast<std::size_t>(size));
	if (vmaFlushAllocation(m_allocator, buffer.allocation, offset, size) != VK_SUCCESS)
		throw std::runtime_error("failed to flush an upload buffer");
}


void DeviceContext::ReadBuffer(const Buffer &buffer, void *data, VkDeviceSize size, VkDeviceSize offset)
{
	if (buffer.mapped == nullptr)
		throw std::logic_error("ReadBuffer: the buffer is not mapped");
	if (offset + size > buffer.size)
		throw std::logic_error("ReadBuffer: the read runs past the end of the buffer");
	if (vmaInvalidateAllocation(m_allocator, buffer.allocation, offset, size) != VK_SUCCESS)
		throw std::runtime_error("failed to invalidate a readback buffer");
	std::memcpy(
	    data, static_cast<const std::byte *>(buffer.mapped) + offset, static_cast<std::size_t>(size));
}


Buffer DeviceContext::CreateDeviceBuffer(
    const void *data, VkDeviceSize size, VkBufferUsageFlags usage, const std::string &name)
{
	Buffer staging =
	    CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::Upload, name + "-staging");
	Buffer buffer{};
	try
	{
		WriteBuffer(staging, data, size);
		buffer = CreateBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUse::Device, name);
		// CopyBuffer waits for the queue, so the staging buffer is free to go when it returns.
		CopyBuffer(staging.handle, buffer.handle, size);
	}
	catch (...)
	{
		DestroyBuffer(buffer);
		DestroyBuffer(staging);
		throw;
	}
	DestroyBuffer(staging);
	return buffer;
}


namespace
{
Image CreateDeviceImage(
    VmaAllocator allocator, const VkImageCreateInfo &imageInfo, const std::string &name)
{
	VmaAllocationCreateInfo allocationInfo{};
	allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	if ((imageInfo.usage &
	     (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) != 0)
		allocationInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	Image image;
	if (vmaCreateImage(
	        allocator, &imageInfo, &allocationInfo, &image.handle, &image.allocation, nullptr) !=
	    VK_SUCCESS)
		throw std::runtime_error("failed to create image '" + name + "'");
	vmaSetAllocationName(allocator, image.allocation, name.c_str());
	return image;
}
} // namespace


Image DeviceContext::CreateImage(
    uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
    VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const std::string &name)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = tiling;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.samples = numSamples;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	const Image image = CreateDeviceImage(m_allocator, imageInfo, name);
	SetDebugName(image.handle, name);
	return image;
}


Image DeviceContext::CreateCubeImage(
    uint32_t size, uint32_t mipLevels, VkFormat format, VkImageUsageFlags usage,
    const std::string &name)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent = {size, size, 1};
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 6;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usage;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	const Image image = CreateDeviceImage(m_allocator, imageInfo, name);
	SetDebugName(image.handle, name);
	return image;
}


void DeviceContext::DestroyImage(Image &image)
{
	if (image.handle != VK_NULL_HANDLE)
		vmaDestroyImage(m_allocator, image.handle, image.allocation);
	image = {};
}
} // namespace Jettison::Render
