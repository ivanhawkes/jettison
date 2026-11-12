#include "vulkan/TextureUpload.h"

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <vector>

namespace Jettison::Render
{
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
} // namespace Jettison::Render
