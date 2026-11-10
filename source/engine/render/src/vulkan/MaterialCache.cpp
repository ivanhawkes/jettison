#include <jettison/render/vulkan/MaterialCache.h>

// STB.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

#include <jettison/render/vulkan/Model.h>

namespace Jettison::Render
{
namespace
{
constexpr VkFormat kBaseColorFormat = VK_FORMAT_R8G8B8A8_SRGB;

// The address of a Model is the key its materials live under, so it is the identifier a reader of
// a validation message has to go look up. Names built from it point at the same model from the
// set, the texture and the vertex buffer.
std::string ModelKey(const Model *pModel)
{
	return "model-" + std::to_string(reinterpret_cast<uintptr_t>(pModel));
}
} // namespace


void MaterialCache::Init(VkDescriptorSetLayout materialLayout)
{
	m_layout = materialLayout;

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	if (vkCreateSampler(m_pDeviceContext->GetLogicalDevice(), &samplerInfo, nullptr, &m_sampler) !=
	    VK_SUCCESS)
		throw std::runtime_error("failed to create texture sampler");
	m_pDeviceContext->SetDebugName(m_sampler, "material-sampler");

	const unsigned char whitePixel[] = {255, 255, 255, 255};
	m_fallback = CreateTexture(whitePixel, 1, 1, "fallback-texture");
}


void MaterialCache::Destroy()
{
	Forget();
	DestroyTexture(m_fallback);
	vkDestroySampler(m_pDeviceContext->GetLogicalDevice(), m_sampler, nullptr);
	m_sampler = VK_NULL_HANDLE;
}


void MaterialCache::Forget()
{
	for (auto &[model, materials] : m_materials)
	{
		(void)model;
		for (Material &material : materials)
		{
			vkDestroyDescriptorPool(m_pDeviceContext->GetLogicalDevice(), material.pool, nullptr);
			if (material.ownsTexture)
				DestroyTexture(material.texture);
		}
	}
	m_materials.clear();
}


VkDescriptorSet MaterialCache::Ensure(const Model &model, std::size_t primitiveIndex)
{
	if (primitiveIndex >= model.m_primitives.size())
		throw std::runtime_error("Model primitive index is out of range");
	auto &materials = m_materials[&model];
	if (primitiveIndex < materials.size())
		return materials[primitiveIndex].set;
	if (primitiveIndex != materials.size())
		throw std::runtime_error("Model primitive materials must be created in order");

	const std::string name = ModelKey(&model) + "-p" + std::to_string(primitiveIndex);
	Material material;
	const auto &imageData = model.m_primitives[primitiveIndex].baseColorImageData;
	if (!imageData.empty())
	{
		if (imageData.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
			throw std::runtime_error("glTF base-color image is too large to decode");
		int width = 0;
		int height = 0;
		int channels = 0;
		const auto *encoded = reinterpret_cast<const stbi_uc *>(imageData.data());
		stbi_uc *pixels = stbi_load_from_memory(
		    encoded, static_cast<int>(imageData.size()), &width, &height, &channels, STBI_rgb_alpha);
		if (!pixels)
			throw std::runtime_error("Failed to decode glTF base-color image");
		try
		{
			material.texture = CreateTexture(pixels, width, height, "texture-" + name);
		}
		catch (...)
		{
			stbi_image_free(pixels);
			throw;
		}
		stbi_image_free(pixels);
		material.ownsTexture = true;
	}

	const VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 1;
	if (vkCreateDescriptorPool(
	        m_pDeviceContext->GetLogicalDevice(), &poolInfo, nullptr, &material.pool) != VK_SUCCESS)
	{
		if (material.ownsTexture)
			DestroyTexture(material.texture);
		throw std::runtime_error("Failed to create a material descriptor pool");
	}
	m_pDeviceContext->SetDebugName(material.pool, name + "-pool");

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = material.pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_layout;
	if (vkAllocateDescriptorSets(m_pDeviceContext->GetLogicalDevice(), &allocInfo, &material.set) !=
	    VK_SUCCESS)
	{
		vkDestroyDescriptorPool(m_pDeviceContext->GetLogicalDevice(), material.pool, nullptr);
		if (material.ownsTexture)
			DestroyTexture(material.texture);
		throw std::runtime_error("Failed to allocate a material descriptor set");
	}
	m_pDeviceContext->SetDebugName(material.set, name + "-set");

	const VkDescriptorImageInfo imageInfo{
	    m_sampler, material.ownsTexture ? material.texture.view : m_fallback.view,
	    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = material.set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &imageInfo;
	vkUpdateDescriptorSets(m_pDeviceContext->GetLogicalDevice(), 1, &write, 0, nullptr);

	materials.push_back(material);
	return material.set;
}


MaterialCache::Texture MaterialCache::CreateTexture(
    const unsigned char *pixels, int width, int height, const std::string &name)
{
	const VkDeviceSize imageSize =
	    static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4;
	Texture texture;
	texture.mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

	Buffer staging = m_pDeviceContext->CreateBuffer(
	    imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::Upload, name + "-staging");
	try
	{
		m_pDeviceContext->WriteBuffer(staging, pixels, imageSize);
		texture.image = m_pDeviceContext->CreateImage(
		    static_cast<uint32_t>(width), static_cast<uint32_t>(height), texture.mipLevels,
		    VK_SAMPLE_COUNT_1_BIT, kBaseColorFormat, VK_IMAGE_TILING_OPTIMAL,
		    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
		        VK_IMAGE_USAGE_SAMPLED_BIT,
		    name);
		m_pDeviceContext->TransitionImageLayout(
		    texture.image.handle, kBaseColorFormat, VK_IMAGE_LAYOUT_UNDEFINED,
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, texture.mipLevels);
		// The copy waits for the queue, so the staging buffer is free to go after it.
		m_pDeviceContext->CopyBufferToImage(
		    staging.handle, texture.image.handle, static_cast<uint32_t>(width),
		    static_cast<uint32_t>(height));
		GenerateMipmaps(texture.image.handle, width, height, texture.mipLevels);
		texture.view = m_pDeviceContext->CreateImageView(
		    texture.image.handle, kBaseColorFormat, VK_IMAGE_ASPECT_COLOR_BIT, texture.mipLevels);
		m_pDeviceContext->SetDebugName(texture.view, name + "-view");
	}
	catch (...)
	{
		DestroyTexture(texture);
		m_pDeviceContext->DestroyBuffer(staging);
		throw;
	}
	m_pDeviceContext->DestroyBuffer(staging);
	return texture;
}


void MaterialCache::DestroyTexture(Texture &texture)
{
	vkDestroyImageView(m_pDeviceContext->GetLogicalDevice(), texture.view, nullptr);
	m_pDeviceContext->DestroyImage(texture.image);
	texture = {};
}


void MaterialCache::GenerateMipmaps(VkImage image, int32_t width, int32_t height, uint32_t mipLevels)
{
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(
	    m_pDeviceContext->GetPhysicalDevice(), kBaseColorFormat, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
		throw std::runtime_error("texture image format doesn't support linear blitting");

	VkCommandBuffer commandBuffer = m_pDeviceContext->BeginSingleTimeCommands();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = width;
	int32_t mipHeight = height;
	for (uint32_t i = 1; i < mipLevels; ++i)
	{
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(
		    commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
		    nullptr, 0, nullptr, 1, &barrier);

		VkImageBlit blit{};
		blit.srcOffsets[0] = {0, 0, 0};
		blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
		blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
		blit.dstOffsets[0] = {0, 0, 0};
		blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
		blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
		vkCmdBlitImage(
		    commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
		    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(
		    commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
		    nullptr, 0, nullptr, 1, &barrier);

		if (mipWidth > 1)
			mipWidth /= 2;
		if (mipHeight > 1)
			mipHeight /= 2;
	}

	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(
	    commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
	    nullptr, 0, nullptr, 1, &barrier);

	m_pDeviceContext->EndSingleTimeCommands(commandBuffer);
}
} // namespace Jettison::Render
