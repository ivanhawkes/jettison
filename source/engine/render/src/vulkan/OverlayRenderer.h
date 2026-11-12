#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <jettison/render/Overlay.h>

#include "vulkan/DeviceContext.h"

namespace Jettison::Render
{
class PipelineCache;

// Draws a Render::Overlay into the output pass, after the scene is tonemapped and before the dev
// tools: the overlay program, its vertices and indices copied into the frame slot's buffers, and a
// device copy of each texture it names, made the first time a draw names it. A texture the overlay
// no longer holds is released once the frames in flight that may read it are done.
class OverlayRenderer
{
  public:
	// The pass draws into one single-sampled attachment of colorFormat, which encodes sRGB itself
	// when encodesSrgb is set.
	OverlayRenderer(
	    std::shared_ptr<DeviceContext> pDeviceContext, PipelineCache &pipelines, VkFormat colorFormat,
	    bool encodesSrgb, std::uint32_t framesInFlight);

	OverlayRenderer(const OverlayRenderer &) = delete;
	OverlayRenderer &operator=(const OverlayRenderer &) = delete;

	// The layouts, the sampler, the white texture an untextured draw reads, and the pipeline: a
	// missing program throws here, naming it.
	void Init();
	// The device has to be idle. Safe after an Init that threw part-way, and twice.
	void Destroy();

	// Once a frame, after the slot's fence and before its commands are recorded: textures the
	// overlay gained are uploaded, ones it lost are retired, and the draws' vertices and indices are
	// written to the slot's buffers.
	void Prepare(std::uint32_t slot, const Overlay &overlay);
	// Inside the output pass's rendering, over a frame of that extent.
	void Record(VkCommandBuffer commandBuffer, std::uint32_t slot, VkExtent2D extent) const;

	// The device textures held, for the tests of a run.
	std::size_t TextureCount() const
	{
		return m_textures.size();
	}

  private:
	struct DeviceTexture
	{
		Image image{};
		VkImageView view{VK_NULL_HANDLE};
		VkDescriptorSet set{VK_NULL_HANDLE};
	};
	struct Retired
	{
		DeviceTexture texture;
		// Frames still to go before nothing in flight reads it.
		std::uint32_t frames{0};
	};
	struct Slot
	{
		Buffer vertices{};
		Buffer indices{};
		// The draws as Prepare found them, with each draw's texture's set resolved.
		std::vector<OverlayDraw> draws;
		std::vector<VkDescriptorSet> sets;
		std::vector<glm::mat4> transforms;
	};

	DeviceTexture Upload(const OverlayTexture &texture, const std::string &name);
	void Free(DeviceTexture &texture);
	void EnsureCapacity(Buffer &buffer, VkDeviceSize bytes, VkBufferUsageFlags usage, const char *name);

	std::shared_ptr<DeviceContext> m_pDeviceContext;
	PipelineCache &m_pipelines;
	VkFormat m_colorFormat{VK_FORMAT_UNDEFINED};
	bool m_encodesSrgb{false};
	std::uint32_t m_framesInFlight{0};

	VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
	VkPipelineLayout m_layout{VK_NULL_HANDLE};
	VkDescriptorPool m_pool{VK_NULL_HANDLE};
	VkSampler m_sampler{VK_NULL_HANDLE};
	VkPipeline m_pipeline{VK_NULL_HANDLE};
	DeviceTexture m_white;
	std::map<OverlayTextureId, DeviceTexture> m_textures;
	std::vector<Retired> m_retired;
	std::vector<Slot> m_slots;
};
} // namespace Jettison::Render
