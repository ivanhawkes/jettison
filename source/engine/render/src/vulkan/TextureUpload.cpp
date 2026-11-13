#include "vulkan/TextureUpload.h"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <vector>

#include <glm/gtc/packing.hpp>

namespace Jettison::Render
{
namespace
{
// The staging buffer's regions copied into the image, its levels from undefined to transfer and then
// to sampled by the fragment stage, in one submission waited on.
void CopyToImage(
    DeviceContext &deviceContext, const Buffer &staging, const Image &image, uint32_t levels,
    const std::vector<VkBufferImageCopy> &regions)
{
	const VkCommandBuffer commandBuffer = deviceContext.BeginSingleTimeCommands();
	VkImageMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	barrier.srcAccessMask = VK_ACCESS_2_NONE;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image.handle;
	barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
	VkDependencyInfo dependency{};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(commandBuffer, &dependency);

	vkCmdCopyBufferToImage(
	    commandBuffer, staging.handle, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	    static_cast<uint32_t>(regions.size()), regions.data());

	barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	vkCmdPipelineBarrier2(commandBuffer, &dependency);
	// Waits for the queue, so the staging buffer is free to go after it.
	deviceContext.EndSingleTimeCommands(commandBuffer);
}
} // namespace


VkFormat ToVkFormat(TextureFormat format)
{
	switch (format)
	{
		case TextureFormat::Rgba8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
		case TextureFormat::Rgba8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
		case TextureFormat::Bc7Unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
		case TextureFormat::Bc7Srgb: return VK_FORMAT_BC7_SRGB_BLOCK;
	}
	return VK_FORMAT_UNDEFINED;
}


void UploadTexture(
    DeviceContext &deviceContext, const TextureData &data, const std::string &name, Image &image,
    VkImageView &view)
{
	const VkFormat format = ToVkFormat(data.format);
	if (IsBlockCompressed(data.format) && !deviceContext.HasBlockCompression())
		throw std::runtime_error("Texture '" + name + "' is BC7, which the device does not sample");
	const auto levels = static_cast<uint32_t>(data.levels.size());
	if (levels == 0 || data.bytes.empty())
		throw std::runtime_error("Texture '" + name + "' has no texels to upload");

	Buffer staging = deviceContext.CreateBuffer(
	    data.bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::Upload, name + "-staging");
	try
	{
		deviceContext.WriteBuffer(staging, data.bytes.data(), data.bytes.size());
		image = deviceContext.CreateImage(
		    data.width, data.height, levels, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
		    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, name);

		std::vector<VkBufferImageCopy> regions;
		regions.reserve(levels);
		for (uint32_t level = 0; level < levels; ++level)
		{
			VkBufferImageCopy region{};
			region.bufferOffset = data.levels[level].offset;
			region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1};
			region.imageExtent = {data.levels[level].width, data.levels[level].height, 1};
			regions.push_back(region);
		}
		CopyToImage(deviceContext, staging, image, levels, regions);

		view = deviceContext.CreateImageView(image.handle, format, VK_IMAGE_ASPECT_COLOR_BIT, levels);
		deviceContext.SetDebugName(view, name + "-view");
	}
	catch (...)
	{
		vkDestroyImageView(deviceContext.GetLogicalDevice(), view, nullptr);
		view = VK_NULL_HANDLE;
		deviceContext.DestroyImage(image);
		deviceContext.DestroyBuffer(staging);
		throw;
	}
	deviceContext.DestroyBuffer(staging);
}


void UploadGrade(
    DeviceContext &deviceContext, const ColourLut &lut, const std::string &name, Image &image,
    VkImageView &view)
{
	constexpr VkFormat kFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	std::vector<std::uint16_t> texels;
	texels.reserve(lut.values.size() * 4);
	for (const glm::vec3 &value : lut.values)
		for (const float channel : {value.x, value.y, value.z, 1.0f})
			texels.push_back(glm::packHalf1x16(channel));
	const VkDeviceSize bytes = texels.size() * sizeof(std::uint16_t);

	Buffer staging = deviceContext.CreateBuffer(
	    bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::Upload, name + "-staging");
	try
	{
		deviceContext.WriteBuffer(staging, texels.data(), bytes);
		image = deviceContext.CreateVolumeImage(
		    lut.size, kFormat, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, name);
		VkBufferImageCopy region{};
		region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		region.imageExtent = {lut.size, lut.size, lut.size};
		CopyToImage(deviceContext, staging, image, 1, {region});
		view = deviceContext.CreateImageView(
		    image.handle, kFormat, VK_IMAGE_VIEW_TYPE_3D, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1});
		deviceContext.SetDebugName(view, name + "-view");
	}
	catch (...)
	{
		vkDestroyImageView(deviceContext.GetLogicalDevice(), view, nullptr);
		view = VK_NULL_HANDLE;
		deviceContext.DestroyImage(image);
		deviceContext.DestroyBuffer(staging);
		throw;
	}
	deviceContext.DestroyBuffer(staging);
}
} // namespace Jettison::Render
