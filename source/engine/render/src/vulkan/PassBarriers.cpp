#include "PassBarriers.h"

#include <stdexcept>

namespace Jettison::Render
{
namespace
{
constexpr VkPipelineStageFlags2 kDepthStencilStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                                                      VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
                                                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
constexpr VkAccessFlags2 kDepthStencilAccess =
    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
} // namespace

UsageScope DestinationScope(Usage usage)
{
	switch (usage)
	{
		case Usage::ColorAttachment:
			return {
			    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			    VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		// A multisample resolve reads its source and writes its target at the colour-output stage, as
		// colour accesses, whether the attachments are colour or depth and stencil (the spec's render
		// pass chapter), so an aspect that is resolved from or into is synchronised there too.
		case Usage::DepthAttachment:
			return {kDepthStencilStages, kDepthStencilAccess, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
		case Usage::StencilAttachment:
			return {
			    kDepthStencilStages, kDepthStencilAccess, VK_IMAGE_LAYOUT_STENCIL_ATTACHMENT_OPTIMAL};
		case Usage::StencilRead:
			return {
			    VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
			        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT |
			        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			    VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			    VK_IMAGE_LAYOUT_STENCIL_READ_ONLY_OPTIMAL};
		case Usage::SampledFragment:
			return {
			    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		case Usage::SampledCompute:
			return {
			    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		case Usage::StorageReadVertex:
			return {
			    VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
			    VK_IMAGE_LAYOUT_UNDEFINED};
		case Usage::StorageReadFragment:
			return {
			    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
			    VK_IMAGE_LAYOUT_UNDEFINED};
		case Usage::StorageReadCompute:
			return {
			    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
			    VK_IMAGE_LAYOUT_GENERAL};
		case Usage::StorageWriteCompute:
			return {
			    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
			    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
			    VK_IMAGE_LAYOUT_GENERAL};
		// Every transfer command, fills as well as copies: vkCmdFillBuffer is a clear, which the
		// copy stage alone does not cover.
		case Usage::TransferRead:
			return {
			    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
			    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
		case Usage::TransferWrite:
			return {
			    VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
			    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
		case Usage::Present:
			return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
	}
	throw std::logic_error("DestinationScope: a usage the table does not have");
}

UsageScope SourceScope(Usage usage)
{
	if (usage == Usage::Present)
		return {
		    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE,
		    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
	UsageScope scope = DestinationScope(usage);
	scope.access &= VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
	                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
	                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT;
	return scope;
}

VulkanBarriers Translate(std::span<const Barrier> barriers, std::span<const BoundResource> bound)
{
	VulkanBarriers translated;
	for (const Barrier &barrier : barriers)
	{
		const BoundResource &resource = bound[barrier.resource.index];
		VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;
		VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
		for (const Usage before : barrier.before)
		{
			const UsageScope scope = SourceScope(before);
			srcStage |= scope.stage;
			srcAccess |= scope.access;
		}
		const UsageScope after = DestinationScope(barrier.after);

		if (resource.image != VK_NULL_HANDLE)
		{
			VkImageMemoryBarrier2 image{};
			image.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			image.srcStageMask = srcStage;
			image.srcAccessMask = srcAccess;
			image.dstStageMask = after.stage;
			image.dstAccessMask = after.access;
			image.oldLayout = barrier.discard || barrier.before.empty()
			                      ? VK_IMAGE_LAYOUT_UNDEFINED
			                      : DestinationScope(barrier.before.back()).layout;
			image.newLayout = after.layout;
			image.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			image.image = resource.image;
			image.subresourceRange = {
			    resource.aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
			translated.images.push_back(image);
		}
		else if (resource.buffer != VK_NULL_HANDLE)
		{
			if (srcStage == VK_PIPELINE_STAGE_2_NONE)
				continue;
			VkBufferMemoryBarrier2 buffer{};
			buffer.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
			buffer.srcStageMask = srcStage;
			buffer.srcAccessMask = srcAccess;
			buffer.dstStageMask = after.stage;
			buffer.dstAccessMask = after.access;
			buffer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			buffer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			buffer.buffer = resource.buffer;
			buffer.offset = 0;
			buffer.size = VK_WHOLE_SIZE;
			translated.buffers.push_back(buffer);
		}
		else
		{
			throw std::logic_error("A planned barrier names a resource that is not bound this frame");
		}
	}
	return translated;
}

void RecordBarriers(
    VkCommandBuffer commandBuffer, std::span<const Barrier> barriers,
    std::span<const BoundResource> bound)
{
	const VulkanBarriers translated = Translate(barriers, bound);
	if (translated.images.empty() && translated.buffers.empty())
		return;
	VkDependencyInfo dependency{};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = static_cast<uint32_t>(translated.images.size());
	dependency.pImageMemoryBarriers = translated.images.data();
	dependency.bufferMemoryBarrierCount = static_cast<uint32_t>(translated.buffers.size());
	dependency.pBufferMemoryBarriers = translated.buffers.data();
	vkCmdPipelineBarrier2(commandBuffer, &dependency);
}
} // namespace Jettison::Render
