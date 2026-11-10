#include <jettison/render/vulkan/Environment.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <jettison/render/Scene.h>
#include <jettison/render/vulkan/PipelineCache.h>

namespace Jettison::Render
{
namespace
{
// sky.frag built as sky_capture.
struct CapturePushConstants
{
	std::uint32_t face{0};
	float pixelAngle{0.0f};
};

// prefilter.frag.
struct PrefilterPushConstants
{
	std::uint32_t face{0};
	std::uint32_t sampleCount{0};
	float roughness{0.0f};
	float sourceSize{0.0f};
	float sourceMaxLevel{0.0f};
};

// brdf_lut.frag.
struct BrdfLutPushConstants
{
	std::uint32_t sampleCount{0};
};

// Filtered importance sampling reads a coarser level of the capture the fewer samples there are,
// so these buy smoothness, not correctness; the lookup table is integrated once, and can afford
// more.
constexpr std::uint32_t kSpecularSamples = 128;
constexpr std::uint32_t kIrradianceSamples = 128;
constexpr std::uint32_t kBrdfLutSamples = 512;

std::uint32_t MipLevels(std::uint32_t size)
{
	std::uint32_t levels = 1;
	while ((size >> levels) != 0)
		++levels;
	return levels;
}

void Barrier(
    VkCommandBuffer commandBuffer, VkImage image, std::uint32_t baseLevel, std::uint32_t levels,
    std::uint32_t layers, VkImageLayout oldLayout, VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess)
{
	VkImageMemoryBarrier2 barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = srcStage;
	barrier.srcAccessMask = srcAccess;
	barrier.dstStageMask = dstStage;
	barrier.dstAccessMask = dstAccess;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseLevel, levels, 0, layers};
	VkDependencyInfo dependency{};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.imageMemoryBarrierCount = 1;
	dependency.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

// From nothing to a colour attachment, every level and layer.
void ToAttachment(
    VkCommandBuffer commandBuffer, VkImage image, std::uint32_t levels, std::uint32_t layers)
{
	Barrier(
	    commandBuffer, image, 0, levels, layers, VK_IMAGE_LAYOUT_UNDEFINED,
	    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
	    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
}

// From the attachment the bake rendered to, to what the frame's fragment shaders sample.
void AttachmentToSampled(
    VkCommandBuffer commandBuffer, VkImage image, std::uint32_t levels, std::uint32_t layers)
{
	Barrier(
	    commandBuffer, image, 0, levels, layers, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
	    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
	    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

RenderState BakeState(VkFormat format)
{
	RenderState state;
	state.vertices = VertexLayout::None;
	state.cullMode = VK_CULL_MODE_NONE;
	state.depthTest = false;
	state.depthWrite = false;
	state.colorFormat = format;
	return state;
}
} // namespace


GpuSky PackSky(const SkyComponent &sky)
{
	GpuSky gpu;
	const float radius =
	    sky.stars > 0.0f ? std::sqrt(sky.stars / (2.0f * std::numbers::pi_v<float>)) : 0.0f;
	const bool stars = radius > 0.0f && sky.starFlux > 0.0f;
	gpu.stars = glm::vec4(radius, sky.starFlux, stars ? 0.06f / radius : 0.0f, stars ? 1.0f : 0.0f);
	gpu.band = glm::vec4(glm::normalize(sky.bandNormal), sky.bandWidth);
	gpu.bandColor = glm::vec4(sky.bandColor, 0.0f);
	gpu.nebula = glm::vec4(sky.nebulaColor, sky.nebulaScale);
	gpu.seed = glm::uvec4(sky.seed, 0u, 0u, 0u);
	return gpu;
}


Environment::Environment(std::shared_ptr<DeviceContext> pDeviceContext, PipelineCache &pipelines)
    : m_pDeviceContext{std::move(pDeviceContext)}, m_pipelines{pipelines}
{
}


void Environment::Init()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS)
		throw std::runtime_error("failed to create the environment sampler");
	m_pDeviceContext->SetDebugName(m_sampler, "environment-sampler");

	CreateLayouts();
	m_capturePipeline = m_pipelines.Get({"sky_capture", BakeState(kCubeFormat), m_captureLayout});
	m_specularPipeline =
	    m_pipelines.Get({"prefilter_specular", BakeState(kCubeFormat), m_prefilterLayout});
	m_irradiancePipeline =
	    m_pipelines.Get({"prefilter_irradiance", BakeState(kCubeFormat), m_prefilterLayout});
	m_brdfLutPipeline = m_pipelines.Get({"brdf_lut", BakeState(kBrdfLutFormat), m_brdfLutLayout});

	BakeBrdfLut();
	Bake(nullptr);
}


void Environment::Destroy()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	DestroyCube(m_specular);
	DestroyCube(m_irradiance);
	vkDestroyImageView(device, m_brdfLutView, nullptr);
	m_brdfLutView = VK_NULL_HANDLE;
	m_pDeviceContext->DestroyImage(m_brdfLut);
	// The pipelines are the pipeline cache's, which destroys them.
	m_capturePipeline = VK_NULL_HANDLE;
	m_specularPipeline = VK_NULL_HANDLE;
	m_irradiancePipeline = VK_NULL_HANDLE;
	m_brdfLutPipeline = VK_NULL_HANDLE;
	vkDestroyPipelineLayout(device, m_brdfLutLayout, nullptr);
	vkDestroyPipelineLayout(device, m_prefilterLayout, nullptr);
	vkDestroyPipelineLayout(device, m_captureLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_prefilterSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_captureSetLayout, nullptr);
	vkDestroySampler(device, m_sampler, nullptr);
	m_brdfLutLayout = VK_NULL_HANDLE;
	m_prefilterLayout = VK_NULL_HANDLE;
	m_captureLayout = VK_NULL_HANDLE;
	m_prefilterSetLayout = VK_NULL_HANDLE;
	m_captureSetLayout = VK_NULL_HANDLE;
	m_sampler = VK_NULL_HANDLE;
}


void Environment::CreateLayouts()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	const auto createSetLayout = [&](VkDescriptorType type, const char *name)
	{
		const VkDescriptorSetLayoutBinding binding{0, type, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
		VkDescriptorSetLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.bindingCount = 1;
		info.pBindings = &binding;
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
			throw std::runtime_error(std::string{"failed to create descriptor set layout "} + name);
		m_pDeviceContext->SetDebugName(layout, name);
		return layout;
	};
	const auto createLayout =
	    [&](VkDescriptorSetLayout setLayout, std::uint32_t pushSize, const char *name)
	{
		const VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize};
		VkPipelineLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		info.setLayoutCount = setLayout != VK_NULL_HANDLE ? 1u : 0u;
		info.pSetLayouts = &setLayout;
		info.pushConstantRangeCount = 1;
		info.pPushConstantRanges = &range;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (vkCreatePipelineLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
			throw std::runtime_error(std::string{"failed to create pipeline layout "} + name);
		m_pDeviceContext->SetDebugName(layout, name);
		return layout;
	};

	m_captureSetLayout = createSetLayout(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, "sky-capture-set-layout");
	m_prefilterSetLayout =
	    createSetLayout(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, "prefilter-set-layout");
	m_captureLayout =
	    createLayout(m_captureSetLayout, sizeof(CapturePushConstants), "sky-capture-pipeline-layout");
	m_prefilterLayout =
	    createLayout(m_prefilterSetLayout, sizeof(PrefilterPushConstants), "prefilter-pipeline-layout");
	m_brdfLutLayout =
	    createLayout(VK_NULL_HANDLE, sizeof(BrdfLutPushConstants), "brdf-lut-pipeline-layout");
}


Environment::Cube Environment::CreateCube(
    std::uint32_t size, std::uint32_t levels, VkImageUsageFlags usage, const char *name)
{
	Cube cube;
	cube.image = m_pDeviceContext->CreateCubeImage(size, levels, kCubeFormat, usage, name);
	try
	{
		cube.view = m_pDeviceContext->CreateImageView(
		    cube.image.handle, kCubeFormat, VK_IMAGE_VIEW_TYPE_CUBE,
		    {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 6});
	}
	catch (...)
	{
		m_pDeviceContext->DestroyImage(cube.image);
		throw;
	}
	m_pDeviceContext->SetDebugName(cube.view, std::string{name} + "-view");
	return cube;
}


void Environment::DestroyCube(Cube &cube)
{
	vkDestroyImageView(m_pDeviceContext->GetLogicalDevice(), cube.view, nullptr);
	m_pDeviceContext->DestroyImage(cube.image);
	cube = {};
}


void Environment::RenderFace(
    VkCommandBuffer commandBuffer, VkImageView target, std::uint32_t size, VkPipeline pipeline,
    VkPipelineLayout layout, VkDescriptorSet set, const void *pushConstants,
    std::uint32_t pushSize) const
{
	VkRenderingAttachmentInfo color{};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = target;
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// The triangle covers every texel.
	color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea = {{0, 0}, {size, size}};
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;

	vkCmdBeginRendering(commandBuffer, &rendering);
	const VkViewport viewport{0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size),
	                          0.0f, 1.0f};
	const VkRect2D scissor{{0, 0}, {size, size}};
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	if (set != VK_NULL_HANDLE)
		vkCmdBindDescriptorSets(
		    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
	vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, pushConstants);
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);
	vkCmdEndRendering(commandBuffer);
}


void Environment::BakeBrdfLut()
{
	m_brdfLut = m_pDeviceContext->CreateImage(
	    kBrdfLutSize, kBrdfLutSize, 1, VK_SAMPLE_COUNT_1_BIT, kBrdfLutFormat, VK_IMAGE_TILING_OPTIMAL,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "brdf-lut");
	m_brdfLutView = m_pDeviceContext->CreateImageView(
	    m_brdfLut.handle, kBrdfLutFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	m_pDeviceContext->SetDebugName(m_brdfLutView, "brdf-lut-view");

	const VkCommandBuffer commandBuffer = m_pDeviceContext->BeginSingleTimeCommands();
	m_pDeviceContext->BeginDebugLabel(commandBuffer, "brdf-lut");
	ToAttachment(commandBuffer, m_brdfLut.handle, 1, 1);
	const BrdfLutPushConstants push{kBrdfLutSamples};
	RenderFace(
	    commandBuffer, m_brdfLutView, kBrdfLutSize, m_brdfLutPipeline, m_brdfLutLayout, VK_NULL_HANDLE,
	    &push, sizeof(push));
	AttachmentToSampled(commandBuffer, m_brdfLut.handle, 1, 1);
	m_pDeviceContext->EndDebugLabel(commandBuffer);
	m_pDeviceContext->EndSingleTimeCommands(commandBuffer);
}


void Environment::Bake(const SkyComponent *pSky)
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	constexpr VkImageUsageFlags kTargetUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
	                                           VK_IMAGE_USAGE_SAMPLED_BIT |
	                                           VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const std::uint32_t captureLevels = MipLevels(kCaptureSize);

	Cube specular;
	Cube irradiance;
	// What only the bake uses, freed when it is done.
	Cube capture;
	Buffer skyBuffer{};
	VkDescriptorPool pool = VK_NULL_HANDLE;
	std::vector<VkImageView> faceViews;
	const auto release = [&]
	{
		for (const VkImageView view : faceViews)
			vkDestroyImageView(device, view, nullptr);
		faceViews.clear();
		vkDestroyDescriptorPool(device, pool, nullptr);
		pool = VK_NULL_HANDLE;
		m_pDeviceContext->DestroyBuffer(skyBuffer);
		DestroyCube(capture);
	};
	// 2D views of each level of each face of a cube, to render into, made before recording begins
	// so nothing between the begin and the submit can throw. By level, then face.
	const auto faceViewsOf = [&](const Cube &cube, std::uint32_t levels)
	{
		const std::size_t first = faceViews.size();
		for (std::uint32_t level = 0; level < levels; ++level)
			for (std::uint32_t face = 0; face < 6; ++face)
				faceViews.push_back(m_pDeviceContext->CreateImageView(
				    cube.image.handle, kCubeFormat, VK_IMAGE_VIEW_TYPE_2D,
				    {VK_IMAGE_ASPECT_COLOR_BIT, level, 1, face, 1}));
		return first;
	};

	try
	{
		specular = CreateCube(kSpecularSize, kSpecularLevels, kTargetUsage, "environment-specular");
		irradiance = CreateCube(kIrradianceSize, 1, kTargetUsage, "environment-irradiance");
		VkDescriptorSet captureSet = VK_NULL_HANDLE;
		VkDescriptorSet prefilterSet = VK_NULL_HANDLE;
		if (pSky != nullptr)
		{
			capture = CreateCube(
			    kCaptureSize, captureLevels, kTargetUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
			    "environment-capture");
			const GpuSky sky = PackSky(*pSky);
			skyBuffer = m_pDeviceContext->CreateBuffer(
			    sizeof(GpuSky), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUse::Upload, "sky-capture");
			m_pDeviceContext->WriteBuffer(skyBuffer, &sky, sizeof(sky));

			const std::array<VkDescriptorPoolSize, 2> poolSizes{{
			    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
			    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
			}};
			VkDescriptorPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes = poolSizes.data();
			poolInfo.maxSets = 2;
			if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS)
				throw std::runtime_error("failed to create the environment bake's descriptor pool");
			m_pDeviceContext->SetDebugName(pool, "environment-bake-pool");
			const std::array<VkDescriptorSetLayout, 2> setLayouts{
			    m_captureSetLayout, m_prefilterSetLayout};
			std::array<VkDescriptorSet, 2> sets{};
			VkDescriptorSetAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = pool;
			allocInfo.descriptorSetCount = static_cast<std::uint32_t>(sets.size());
			allocInfo.pSetLayouts = setLayouts.data();
			if (vkAllocateDescriptorSets(device, &allocInfo, sets.data()) != VK_SUCCESS)
				throw std::runtime_error("failed to allocate the environment bake's descriptor sets");
			captureSet = sets[0];
			prefilterSet = sets[1];
			const VkDescriptorBufferInfo skyInfo{skyBuffer.handle, 0, sizeof(GpuSky)};
			const VkDescriptorImageInfo captureInfo{
			    m_sampler, capture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
			std::array<VkWriteDescriptorSet, 2> writes{};
			writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[0].dstSet = captureSet;
			writes[0].descriptorCount = 1;
			writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writes[0].pBufferInfo = &skyInfo;
			writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[1].dstSet = prefilterSet;
			writes[1].descriptorCount = 1;
			writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[1].pImageInfo = &captureInfo;
			vkUpdateDescriptorSets(
			    device, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
		}
		const std::size_t captureFaces = pSky != nullptr ? faceViewsOf(capture, 1) : 0;
		const std::size_t specularFaces = pSky != nullptr ? faceViewsOf(specular, kSpecularLevels) : 0;
		const std::size_t irradianceFaces = pSky != nullptr ? faceViewsOf(irradiance, 1) : 0;

		const VkCommandBuffer commandBuffer = m_pDeviceContext->BeginSingleTimeCommands();
		m_pDeviceContext->BeginDebugLabel(commandBuffer, "environment-bake");
		if (pSky == nullptr)
		{
			// No sky: black, which lights nothing.
			const VkClearColorValue black{{0.0f, 0.0f, 0.0f, 1.0f}};
			for (const auto &[cube, levels] :
			     {std::pair{&specular, kSpecularLevels}, std::pair{&irradiance, 1u}})
			{
				Barrier(
				    commandBuffer, cube->image.handle, 0, levels, 6, VK_IMAGE_LAYOUT_UNDEFINED,
				    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
				    VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
				const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 6};
				vkCmdClearColorImage(
				    commandBuffer, cube->image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
				    &range);
				Barrier(
				    commandBuffer, cube->image.handle, 0, levels, 6,
				    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				    VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
			}
		}
		else
		{
			// The sky, face by face, into the capture's first level.
			ToAttachment(commandBuffer, capture.image.handle, 1, 6);
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				const CapturePushConstants push{face, 2.0f / static_cast<float>(kCaptureSize)};
				RenderFace(
				    commandBuffer, faceViews[captureFaces + face], kCaptureSize, m_capturePipeline,
				    m_captureLayout, captureSet, &push, sizeof(push));
			}
			// Its mip chain, each level a linear blit of the one before, all six faces at once.
			Barrier(
			    commandBuffer, capture.image.handle, 0, 1, 6, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT,
			    VK_ACCESS_2_TRANSFER_READ_BIT);
			Barrier(
			    commandBuffer, capture.image.handle, 1, captureLevels - 1, 6, VK_IMAGE_LAYOUT_UNDEFINED,
			    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
			    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
			for (std::uint32_t level = 1; level < captureLevels; ++level)
			{
				const auto source = static_cast<std::int32_t>(kCaptureSize >> (level - 1));
				const auto target = static_cast<std::int32_t>(kCaptureSize >> level);
				VkImageBlit blit{};
				blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, 6};
				blit.srcOffsets[1] = {source, source, 1};
				blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 6};
				blit.dstOffsets[1] = {target, target, 1};
				vkCmdBlitImage(
				    commandBuffer, capture.image.handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				    capture.image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
				    VK_FILTER_LINEAR);
				Barrier(
				    commandBuffer, capture.image.handle, level, 1, 6,
				    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
				    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
			}
			Barrier(
			    commandBuffer, capture.image.handle, 0, captureLevels, 6,
			    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			    VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
			    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

			// Each specular level at its roughness, from a mirror at the first to 1 at the last.
			PrefilterPushConstants push{};
			push.sourceSize = static_cast<float>(kCaptureSize);
			push.sourceMaxLevel = static_cast<float>(captureLevels - 1);
			ToAttachment(commandBuffer, specular.image.handle, kSpecularLevels, 6);
			for (std::uint32_t level = 0; level < kSpecularLevels; ++level)
			{
				push.sampleCount = level == 0 ? 1u : kSpecularSamples;
				push.roughness = static_cast<float>(level) / GetMaxSpecularLevel();
				for (std::uint32_t face = 0; face < 6; ++face)
				{
					push.face = face;
					RenderFace(
					    commandBuffer, faceViews[specularFaces + std::size_t{level} * 6 + face],
					    kSpecularSize >> level, m_specularPipeline, m_prefilterLayout, prefilterSet,
					    &push, sizeof(push));
				}
			}
			AttachmentToSampled(commandBuffer, specular.image.handle, kSpecularLevels, 6);

			push.sampleCount = kIrradianceSamples;
			push.roughness = 1.0f;
			ToAttachment(commandBuffer, irradiance.image.handle, 1, 6);
			for (std::uint32_t face = 0; face < 6; ++face)
			{
				push.face = face;
				RenderFace(
				    commandBuffer, faceViews[irradianceFaces + face], kIrradianceSize,
				    m_irradiancePipeline, m_prefilterLayout, prefilterSet, &push, sizeof(push));
			}
			AttachmentToSampled(commandBuffer, irradiance.image.handle, 1, 6);
		}
		m_pDeviceContext->EndDebugLabel(commandBuffer);
		// Waits for the queue: every frame submitted before this one is done with it too.
		m_pDeviceContext->EndSingleTimeCommands(commandBuffer);
	}
	catch (...)
	{
		release();
		DestroyCube(specular);
		DestroyCube(irradiance);
		throw;
	}
	release();
	DestroyCube(m_specular);
	DestroyCube(m_irradiance);
	m_specular = specular;
	m_irradiance = irradiance;
}
} // namespace Jettison::Render
