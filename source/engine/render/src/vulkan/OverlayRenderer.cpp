#include "vulkan/OverlayRenderer.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

#include "vulkan/PipelineCache.h"
#include "vulkan/TextureUpload.h"

namespace Jettison::Render
{
namespace
{
// The overlay program's push constants: overlay.glsl's OverlayDraw, for both stages.
struct OverlayPushConstants
{
	glm::mat4 transform{1.0f};
	glm::vec2 translation{0.0f};
	std::uint32_t decodeSrgb{0};
	std::uint32_t padding{0};
};
static_assert(sizeof(OverlayPushConstants) == 80);

// Textures the overlay holds at once: a font's atlas per face and size, and the images its
// documents show. The market screen holds three.
constexpr std::uint32_t kMaxTextures = 512;

// From the frame's pixels, y down, to Vulkan's clip space, which is y down too. Depth is RmlUi's
// range for a transformed element (+-10,000 pixels) into zero to one; nothing tests it.
glm::mat4 PixelProjection(VkExtent2D extent)
{
	glm::mat4 projection{1.0f};
	projection[0][0] = 2.0f / static_cast<float>(extent.width);
	projection[1][1] = 2.0f / static_cast<float>(extent.height);
	projection[2][2] = 1.0f / 20000.0f;
	projection[3] = {-1.0f, -1.0f, 0.5f, 1.0f};
	return projection;
}
} // namespace


OverlayRenderer::OverlayRenderer(
    std::shared_ptr<DeviceContext> pDeviceContext, PipelineCache &pipelines, VkFormat colorFormat,
    bool encodesSrgb, std::uint32_t framesInFlight)
    : m_pDeviceContext{std::move(pDeviceContext)}, m_pipelines{pipelines}, m_colorFormat{colorFormat},
      m_encodesSrgb{encodesSrgb}, m_framesInFlight{framesInFlight}
{
}


void OverlayRenderer::Init()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	const VkDescriptorSetLayoutBinding binding{
	    0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
	VkDescriptorSetLayoutCreateInfo setInfo{};
	setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	setInfo.bindingCount = 1;
	setInfo.pBindings = &binding;
	if (vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &m_setLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create the overlay's set layout");
	m_pDeviceContext->SetDebugName(m_setLayout, "overlay-set-layout");

	const VkPushConstantRange range{
	    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OverlayPushConstants)};
	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &m_setLayout;
	layoutInfo.pushConstantRangeCount = 1;
	layoutInfo.pPushConstantRanges = &range;
	if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout) != VK_SUCCESS)
		throw std::runtime_error("failed to create the overlay's pipeline layout");
	m_pDeviceContext->SetDebugName(m_layout, "overlay-pipeline-layout");

	// One set a texture, freed with it.
	const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = kMaxTextures;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_pool) != VK_SUCCESS)
		throw std::runtime_error("failed to create the overlay's descriptor pool");
	m_pDeviceContext->SetDebugName(m_pool, "overlay-descriptor-pool");

	// Linear, so a texture drawn at a fractional offset or scaled by a transform is filtered as a
	// browser filters it; clamped, so a glyph at the edge of the atlas reads none of its neighbour.
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = 0.0f;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
		throw std::runtime_error("failed to create the overlay's sampler");
	m_pDeviceContext->SetDebugName(m_sampler, "overlay-sampler");

	m_white = Upload({1, 1, std::vector<std::byte>(4, std::byte{255})}, "overlay-white");
	m_slots.resize(m_framesInFlight);

	RenderState state;
	state.vertices = VertexLayout::Overlay;
	state.cullMode = VK_CULL_MODE_NONE;
	state.depthTest = false;
	state.depthWrite = false;
	state.blend = BlendMode::Premultiplied;
	state.colorFormat = m_colorFormat;
	m_pipeline = m_pipelines.Get({"overlay", state, m_layout});
}


void OverlayRenderer::Destroy()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	for (Slot &slot : m_slots)
	{
		m_pDeviceContext->DestroyBuffer(slot.vertices);
		m_pDeviceContext->DestroyBuffer(slot.indices);
	}
	m_slots.clear();
	for (auto &[id, texture] : m_textures)
		Free(texture);
	m_textures.clear();
	for (Retired &retired : m_retired)
		Free(retired.texture);
	m_retired.clear();
	Free(m_white);
	// The pipeline is the cache's.
	m_pipeline = VK_NULL_HANDLE;
	if (device == VK_NULL_HANDLE)
		return;
	vkDestroySampler(device, m_sampler, nullptr);
	vkDestroyDescriptorPool(device, m_pool, nullptr);
	vkDestroyPipelineLayout(device, m_layout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_setLayout, nullptr);
	m_sampler = VK_NULL_HANDLE;
	m_pool = VK_NULL_HANDLE;
	m_layout = VK_NULL_HANDLE;
	m_setLayout = VK_NULL_HANDLE;
}


void OverlayRenderer::Prepare(std::uint32_t slotIndex, const Overlay &overlay)
{
	// The slot's fence has been waited on: a texture retired that many frames ago is read by
	// nothing in flight.
	std::erase_if(
	    m_retired,
	    [this](Retired &retired)
	    {
		    if (retired.frames-- > 0)
			    return false;
		    Free(retired.texture);
		    return true;
	    });
	for (auto it = m_textures.begin(); it != m_textures.end();)
	{
		if (overlay.FindTexture(it->first) != nullptr)
		{
			++it;
			continue;
		}
		m_retired.push_back({it->second, m_framesInFlight});
		it = m_textures.erase(it);
	}

	Slot &slot = m_slots.at(slotIndex);
	slot.draws.clear();
	slot.sets.clear();
	slot.transforms = overlay.Transforms();
	for (const OverlayDraw &draw : overlay.Draws())
	{
		VkDescriptorSet set = m_white.set;
		if (draw.texture != kNoOverlayTexture)
		{
			auto found = m_textures.find(draw.texture);
			if (found == m_textures.end())
			{
				// A draw naming a texture the overlay does not hold draws nothing, as RmlUi draws an
				// image that failed to load.
				const OverlayTexture *texture = overlay.FindTexture(draw.texture);
				if (texture == nullptr)
					continue;
				found = m_textures
				            .emplace(
				                draw.texture,
				                Upload(*texture, "overlay-texture-" + std::to_string(draw.texture)))
				            .first;
			}
			set = found->second.set;
		}
		slot.draws.push_back(draw);
		slot.sets.push_back(set);
	}
	if (slot.draws.empty())
		return;

	const VkDeviceSize vertexBytes = overlay.Vertices().size() * sizeof(OverlayVertex);
	const VkDeviceSize indexBytes = overlay.Indices().size() * sizeof(std::uint32_t);
	EnsureCapacity(slot.vertices, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "overlay-vertices");
	EnsureCapacity(slot.indices, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "overlay-indices");
	m_pDeviceContext->WriteBuffer(slot.vertices, overlay.Vertices().data(), vertexBytes);
	m_pDeviceContext->WriteBuffer(slot.indices, overlay.Indices().data(), indexBytes);
}


void OverlayRenderer::Record(
    VkCommandBuffer commandBuffer, std::uint32_t slotIndex, VkExtent2D extent) const
{
	const Slot &slot = m_slots.at(slotIndex);
	if (slot.draws.empty())
		return;
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	const VkDeviceSize offset = 0;
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, &slot.vertices.handle, &offset);
	vkCmdBindIndexBuffer(commandBuffer, slot.indices.handle, 0, VK_INDEX_TYPE_UINT32);

	const glm::mat4 projection = PixelProjection(extent);
	const VkRect2D whole{{0, 0}, extent};
	for (std::size_t index = 0; index < slot.draws.size(); ++index)
	{
		const OverlayDraw &draw = slot.draws[index];
		VkRect2D scissor = whole;
		if (draw.scissor)
		{
			// Clamped to the frame: Vulkan refuses a scissor with a negative offset.
			const std::int32_t left =
			    std::clamp(draw.scissor->x, 0, static_cast<std::int32_t>(extent.width));
			const std::int32_t top =
			    std::clamp(draw.scissor->y, 0, static_cast<std::int32_t>(extent.height));
			const std::int32_t right = std::clamp(
			    draw.scissor->x + draw.scissor->width, left, static_cast<std::int32_t>(extent.width));
			const std::int32_t bottom = std::clamp(
			    draw.scissor->y + draw.scissor->height, top, static_cast<std::int32_t>(extent.height));
			scissor = {
			    {left, top},
			    {static_cast<std::uint32_t>(right - left), static_cast<std::uint32_t>(bottom - top)}};
			if (scissor.extent.width == 0 || scissor.extent.height == 0)
				continue;
		}
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
		vkCmdBindDescriptorSets(
		    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &slot.sets[index], 0,
		    nullptr);
		OverlayPushConstants constants;
		constants.transform =
		    draw.transform ? projection * slot.transforms.at(*draw.transform) : projection;
		constants.translation = draw.translation;
		constants.decodeSrgb = m_encodesSrgb ? 1u : 0u;
		vkCmdPushConstants(
		    commandBuffer, m_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
		    sizeof(constants), &constants);
		vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, draw.firstIndex, draw.vertexOffset, 0);
	}
	vkCmdSetScissor(commandBuffer, 0, 1, &whole);
}


OverlayRenderer::DeviceTexture OverlayRenderer::Upload(
    const OverlayTexture &texture, const std::string &name)
{
	// Encoded sRGB, but uploaded UNORM: the shader multiplies and the sampler filters the encoded
	// values, as a browser does, and the shader decodes once at the end (overlay.frag).
	TextureData data;
	data.format = TextureFormat::Rgba8Unorm;
	data.width = texture.width;
	data.height = texture.height;
	data.levels = {{texture.width, texture.height, 0, texture.texels.size()}};
	data.bytes = texture.texels;

	DeviceTexture uploaded;
	UploadTexture(*m_pDeviceContext, data, name, uploaded.image, uploaded.view);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_setLayout;
	if (vkAllocateDescriptorSets(m_pDeviceContext->GetLogicalDevice(), &allocInfo, &uploaded.set) !=
	    VK_SUCCESS)
	{
		Free(uploaded);
		throw std::runtime_error(
		    std::format(
		        "the overlay holds more than {} textures; '{}' was not added", kMaxTextures, name));
	}
	m_pDeviceContext->SetDebugName(uploaded.set, name + "-set");
	const VkDescriptorImageInfo imageInfo{
	    m_sampler, uploaded.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = uploaded.set;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(m_pDeviceContext->GetLogicalDevice(), 1, &write, 0, nullptr);
	return uploaded;
}


void OverlayRenderer::Free(DeviceTexture &texture)
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	if (texture.set != VK_NULL_HANDLE)
		vkFreeDescriptorSets(device, m_pool, 1, &texture.set);
	if (texture.view != VK_NULL_HANDLE)
		vkDestroyImageView(device, texture.view, nullptr);
	m_pDeviceContext->DestroyImage(texture.image);
	texture = {};
}


void OverlayRenderer::EnsureCapacity(
    Buffer &buffer, VkDeviceSize bytes, VkBufferUsageFlags usage, const char *name)
{
	if (bytes <= buffer.size)
		return;
	// The slot's fence has been waited on, so no submission in flight reads the buffer.
	VkDeviceSize size = std::max<VkDeviceSize>(buffer.size, 4096);
	while (size < bytes)
		size *= 2;
	m_pDeviceContext->DestroyBuffer(buffer);
	buffer = m_pDeviceContext->CreateBuffer(
	    size, usage, MemoryUse::Upload, std::string{name} + "-" + std::to_string(size));
}
} // namespace Jettison::Render
