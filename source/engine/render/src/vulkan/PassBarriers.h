#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <vector>

#include <jettison/render/PassList.h>

namespace Jettison::Render
{
// What a Usage means to Vulkan (synchronization2): the stages that touch the resource, the
// accesses they make, and for an image the layout it has to be in.
struct UsageScope
{
	VkPipelineStageFlags2 stage{VK_PIPELINE_STAGE_2_NONE};
	VkAccessFlags2 access{VK_ACCESS_2_NONE};
	VkImageLayout layout{VK_IMAGE_LAYOUT_UNDEFINED};
};

// The scope a pass's use waits in.
UsageScope DestinationScope(Usage usage);
// The scope a later use waits on: the same stages, and only the writes, since a read leaves
// nothing to make available. Present is the exception: the image comes back through the
// acquire, whose semaphore the submit waits on at the colour-output stage, so the first use
// in the next frame chains on that stage.
UsageScope SourceScope(Usage usage);

// The image or buffer a plan's resource is this frame.
struct BoundResource
{
	VkImage image{VK_NULL_HANDLE};
	VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
	VkBuffer buffer{VK_NULL_HANDLE};
};

struct VulkanBarriers
{
	std::vector<VkImageMemoryBarrier2> images;
	std::vector<VkBufferMemoryBarrier2> buffers;
};

// One Vulkan barrier per planned barrier, against the resources bound this frame. A buffer's
// first use with nothing to wait on needs no barrier and gets none.
VulkanBarriers Translate(std::span<const Barrier> barriers, std::span<const BoundResource> bound);

// Records them, as one vkCmdPipelineBarrier2. Nothing is recorded for an empty set.
void RecordBarriers(
    VkCommandBuffer commandBuffer, std::span<const Barrier> barriers,
    std::span<const BoundResource> bound);
} // namespace Jettison::Render
