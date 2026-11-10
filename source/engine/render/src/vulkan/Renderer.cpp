#include <jettison/render/vulkan/Renderer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include <jettison/render/vulkan/Environment.h>
#include <jettison/render/vulkan/FrameData.h>
#include <jettison/render/vulkan/MaterialCache.h>
#include <jettison/render/vulkan/PipelineCache.h>
#include <jettison/render/vulkan/imgui/ImGuiRenderer.h>

#include "PassBarriers.h"

namespace Jettison::Render
{
namespace
{
// assets/shaders/include/draw.glsl.
struct DrawPushConstants
{
	alignas(16) glm::mat4 model{1.0f};
	uint32_t jointOffset{};
	uint32_t skinned{};
	uint32_t materialOffset{};
	uint32_t padding{};
};
static_assert(sizeof(DrawPushConstants) == 80);
constexpr VkShaderStageFlags kDrawStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

struct OutputPushConstants
{
	float exposure{1.0f};
	uint32_t encodeSrgb{0};
};

// Room for this many lights before the first frame that has more grows the list.
constexpr VkDeviceSize kInitialLights = 16;
// And for this many material parameters, which are vec4s.
constexpr VkDeviceSize kInitialMaterialParameters = 64;

// What the sky draws the sun as when the scene does not say how large it is: the Sun from 1 AU.
constexpr double kDefaultSunAngularRadius = 0.004652;
// The brightest the sun's disc is drawn. Its true radiance, irradiance over the half-degree disc's
// solid angle, is tens of thousands of times its intensity and would overflow the half-float HDR
// target; past this the tonemapper shows white either way.
constexpr float kMaxSunRadiance = 30000.0f;

const SkyComponent *FindSky(const entt::registry &sceneRegistry)
{
	// The scene loader allows one.
	const auto view = sceneRegistry.view<const SkyComponent>();
	return view.empty() ? nullptr : &view.get<const SkyComponent>(view.front());
}
} // namespace


Renderer::Renderer(
    std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Window> pWindow,
    std::shared_ptr<Swapchain> pSwapchain, const Vfs::FileSystem &assets,
    Resource::ResourceManager &resources)
    : m_pDeviceContext{std::move(pDeviceContext)}, m_pWindow{std::move(pWindow)},
      m_pSwapchain{std::move(pSwapchain)}, m_assets{assets}, m_resources{resources}
{
}


// Out of line, where PipelineCache and MaterialCache are complete types. It releases no GPU
// object: Destroy() does, in the order the shell calls it.
Renderer::~Renderer() = default;


void Renderer::Init()
{
	if (m_pDeviceContext->GetMsaaSamples() == VK_SAMPLE_COUNT_1_BIT)
		throw std::runtime_error("the HDR target needs a multisampled colour and depth attachment");
	m_depthFormat = m_pDeviceContext->FindDepthFormat();
	m_pPipelines = std::make_unique<PipelineCache>(m_pDeviceContext, m_assets);
	m_pMaterials = std::make_unique<MaterialCache>(
	    m_pDeviceContext, m_assets, m_resources, *m_pPipelines, kFramesInFlight);

	CreateLayouts();
	CreatePassList();
	CreateTargets();
	// Before the frames: their sets name its cubes and lookup table.
	m_pEnvironment = std::make_unique<Environment>(m_pDeviceContext, *m_pPipelines);
	m_pEnvironment->Init();
	CreateFrames();
	WriteOutputSet();
	CreatePipelines();
	CreateRenderFinishedSemaphores();
}


void Renderer::Destroy()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	DestroyRenderFinishedSemaphores();
	for (FrameData &frame : m_frames)
	{
		if (frame.commandBuffer != VK_NULL_HANDLE)
			vkFreeCommandBuffers(device, m_pDeviceContext->GetCommandPool(), 1, &frame.commandBuffer);
		vkDestroyFence(device, frame.inFlight, nullptr);
		vkDestroySemaphore(device, frame.imageAvailable, nullptr);
		m_pDeviceContext->DestroyBuffer(frame.constants);
		m_pDeviceContext->DestroyBuffer(frame.joints);
		m_pDeviceContext->DestroyBuffer(frame.lights);
		m_pDeviceContext->DestroyBuffer(frame.lines);
		m_pDeviceContext->DestroyBuffer(frame.materials);
		frame = {};
	}
	DestroyTargets();
	if (m_pEnvironment)
		m_pEnvironment->Destroy();
	m_bakedSky.reset();
	if (m_pMaterials)
		m_pMaterials->Destroy();
	if (m_pPipelines)
		m_pPipelines->Destroy();
	m_drawMaterials.clear();
	m_outputPipeline = VK_NULL_HANDLE;
	m_skyPipeline = VK_NULL_HANDLE;
	m_linePipeline = VK_NULL_HANDLE;
	m_lineOverlayPipeline = VK_NULL_HANDLE;

	vkDestroySampler(device, m_outputSampler, nullptr);
	vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
	vkDestroyPipelineLayout(device, m_lineLayout, nullptr);
	vkDestroyPipelineLayout(device, m_outputLayout, nullptr);
	vkDestroyPipelineLayout(device, m_meshLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_outputSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_materialSetLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, m_frameSetLayout, nullptr);
	m_outputSampler = VK_NULL_HANDLE;
	m_descriptorPool = VK_NULL_HANDLE;
	m_outputSet = VK_NULL_HANDLE;
	m_outputLayout = VK_NULL_HANDLE;
	m_lineLayout = VK_NULL_HANDLE;
	m_meshLayout = VK_NULL_HANDLE;
	m_outputSetLayout = VK_NULL_HANDLE;
	m_materialSetLayout = VK_NULL_HANDLE;
	m_frameSetLayout = VK_NULL_HANDLE;
	m_imagesInFlight.clear();
}


void Renderer::CreateLayouts()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	const auto createSetLayout =
	    [&](std::span<const VkDescriptorSetLayoutBinding> bindings, const char *name)
	{
		VkDescriptorSetLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.bindingCount = static_cast<uint32_t>(bindings.size());
		info.pBindings = bindings.data();
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		if (vkCreateDescriptorSetLayout(device, &info, nullptr, &layout) != VK_SUCCESS)
			throw std::runtime_error(std::string{"failed to create descriptor set layout "} + name);
		m_pDeviceContext->SetDebugName(layout, name);
		return layout;
	};

	// assets/shaders/include/frame.glsl, and the environment's bindings in lighting.glsl.
	const std::array<VkDescriptorSetLayoutBinding, 7> frameBindings{{
	    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
	     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
	    {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	}};
	m_frameSetLayout = createSetLayout(frameBindings, "frame-set-layout");
	// assets/shaders/include/material.glsl: every scene program's textures, by slot.
	std::array<VkDescriptorSetLayoutBinding, kMaterialTextureSlots> materialBindings{};
	for (uint32_t slot = 0; slot < kMaterialTextureSlots; ++slot)
		materialBindings[slot] = {
		    slot, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
	m_materialSetLayout = createSetLayout(materialBindings, "material-set-layout");
	const std::array<VkDescriptorSetLayoutBinding, 1> outputBindings{{
	    {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	}};
	m_outputSetLayout = createSetLayout(outputBindings, "output-set-layout");

	const std::array<VkDescriptorSetLayout, 2> meshSets{m_frameSetLayout, m_materialSetLayout};
	const VkPushConstantRange drawRange{kDrawStages, 0, sizeof(DrawPushConstants)};
	VkPipelineLayoutCreateInfo meshInfo{};
	meshInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	meshInfo.setLayoutCount = static_cast<uint32_t>(meshSets.size());
	meshInfo.pSetLayouts = meshSets.data();
	meshInfo.pushConstantRangeCount = 1;
	meshInfo.pPushConstantRanges = &drawRange;
	if (vkCreatePipelineLayout(device, &meshInfo, nullptr, &m_meshLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create the mesh pipeline layout");
	m_pDeviceContext->SetDebugName(m_meshLayout, "mesh-pipeline-layout");

	// The debug lines read the frame's view and projection, and nothing else.
	VkPipelineLayoutCreateInfo lineInfo{};
	lineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	lineInfo.setLayoutCount = 1;
	lineInfo.pSetLayouts = &m_frameSetLayout;
	if (vkCreatePipelineLayout(device, &lineInfo, nullptr, &m_lineLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create the debug line pipeline layout");
	m_pDeviceContext->SetDebugName(m_lineLayout, "debug-line-pipeline-layout");

	const VkPushConstantRange outputRange{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutputPushConstants)};
	VkPipelineLayoutCreateInfo outputInfo{};
	outputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	outputInfo.setLayoutCount = 1;
	outputInfo.pSetLayouts = &m_outputSetLayout;
	outputInfo.pushConstantRangeCount = 1;
	outputInfo.pPushConstantRanges = &outputRange;
	if (vkCreatePipelineLayout(device, &outputInfo, nullptr, &m_outputLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create the output pipeline layout");
	m_pDeviceContext->SetDebugName(m_outputLayout, "output-pipeline-layout");

	// One frame set per frame in flight, and the output set.
	const std::array<VkDescriptorPoolSize, 3> poolSizes{{
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * kFramesInFlight},
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 * kFramesInFlight + 1},
	}};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = kFramesInFlight + 1;
	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("failed to create the frame descriptor pool");
	m_pDeviceContext->SetDebugName(m_descriptorPool, "frame-descriptor-pool");

	// The output pass reads the resolved target texel for texel.
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_NEAREST;
	samplerInfo.minFilter = VK_FILTER_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &m_outputSampler) != VK_SUCCESS)
		throw std::runtime_error("failed to create the output sampler");
	m_pDeviceContext->SetDebugName(m_outputSampler, "output-sampler");

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_outputSetLayout;
	if (vkAllocateDescriptorSets(device, &allocInfo, &m_outputSet) != VK_SUCCESS)
		throw std::runtime_error("failed to allocate the output descriptor set");
	m_pDeviceContext->SetDebugName(m_outputSet, "output-set");
}


void Renderer::CreatePassList()
{
	m_hdrMsaaResource = m_passes.AddImage("hdr-msaa");
	m_depthResource = m_passes.AddImage("depth");
	m_hdrResource = m_passes.AddImage("hdr");
	m_swapchainResource = m_passes.AddImage("swapchain");
	// The resolve is the opaque pass's: dynamic rendering resolves the multisampled target into
	// the single-sampled one as the pass ends, as a colour attachment write.
	m_opaquePass = m_passes.AddPass(
	    "opaque", PassKind::Graphics,
	    {{m_hdrMsaaResource, Usage::ColorAttachment},
	     {m_depthResource, Usage::DepthAttachment},
	     {m_hdrResource, Usage::ColorAttachment}});
	m_outputPass = m_passes.AddPass(
	    "output", PassKind::Graphics,
	    {{m_hdrResource, Usage::SampledFragment}, {m_swapchainResource, Usage::ColorAttachment}});
	m_passes.Export(m_swapchainResource, Usage::Present);
	m_plan = m_passes.Compile();
}


void Renderer::CreateTargets()
{
	const VkExtent2D extent = m_pSwapchain->GetExtents();
	const VkSampleCountFlagBits samples = m_pDeviceContext->GetMsaaSamples();
	m_targets.extent = extent;
	// The multisampled images live only inside the opaque pass: they are cleared on load and not
	// stored, so a tiler can keep them on chip.
	m_targets.hdrMsaa = m_pDeviceContext->CreateImage(
	    extent.width, extent.height, 1, samples, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, "hdr-msaa");
	m_targets.hdrMsaaView = m_pDeviceContext->CreateImageView(
	    m_targets.hdrMsaa.handle, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	m_pDeviceContext->SetDebugName(m_targets.hdrMsaaView, "hdr-msaa-view");

	m_targets.depth = m_pDeviceContext->CreateImage(
	    extent.width, extent.height, 1, samples, m_depthFormat, VK_IMAGE_TILING_OPTIMAL,
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, "depth");
	m_targets.depthView = m_pDeviceContext->CreateImageView(
	    m_targets.depth.handle, m_depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
	m_pDeviceContext->SetDebugName(m_targets.depthView, "depth-view");

	m_targets.hdr = m_pDeviceContext->CreateImage(
	    extent.width, extent.height, 1, VK_SAMPLE_COUNT_1_BIT, kHdrFormat, VK_IMAGE_TILING_OPTIMAL,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "hdr");
	m_targets.hdrView = m_pDeviceContext->CreateImageView(
	    m_targets.hdr.handle, kHdrFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
	m_pDeviceContext->SetDebugName(m_targets.hdrView, "hdr-view");
}


void Renderer::DestroyTargets()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	vkDestroyImageView(device, m_targets.hdrView, nullptr);
	vkDestroyImageView(device, m_targets.depthView, nullptr);
	vkDestroyImageView(device, m_targets.hdrMsaaView, nullptr);
	m_pDeviceContext->DestroyImage(m_targets.hdr);
	m_pDeviceContext->DestroyImage(m_targets.depth);
	m_pDeviceContext->DestroyImage(m_targets.hdrMsaa);
	m_targets = {};
}


void Renderer::CreateFrames()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	for (uint32_t index = 0; index < kFramesInFlight; ++index)
	{
		FrameData &frame = m_frames[index];
		const std::string slot = std::to_string(index);

		VkCommandBufferAllocateInfo commandInfo{};
		commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandInfo.commandPool = m_pDeviceContext->GetCommandPool();
		commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandInfo.commandBufferCount = 1;
		if (vkAllocateCommandBuffers(device, &commandInfo, &frame.commandBuffer) != VK_SUCCESS)
			throw std::runtime_error("failed to allocate a frame's command buffer");
		m_pDeviceContext->SetDebugName(frame.commandBuffer, "command-buffer-" + slot);

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS ||
		    vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS)
			throw std::runtime_error("failed to create a frame's semaphore and fence");
		// The two frame-loop waits in Render read differently, one per frame and one per image,
		// and a message that names the fence is how a reader tells which one fired.
		m_pDeviceContext->SetDebugName(frame.imageAvailable, "image-available-" + slot);
		m_pDeviceContext->SetDebugName(frame.inFlight, "in-flight-" + slot);

		frame.constants = m_pDeviceContext->CreateBuffer(
		    sizeof(FrameUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUse::Upload,
		    "frame-constants-" + slot);
		frame.joints = m_pDeviceContext->CreateBuffer(
		    sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUse::Upload,
		    "joint-palette-" + slot);
		frame.lines = m_pDeviceContext->CreateBuffer(
		    1024 * sizeof(LineVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, MemoryUse::Upload,
		    "debug-lines-" + slot);
		frame.lights = m_pDeviceContext->CreateBuffer(
		    kInitialLights * sizeof(GpuLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, MemoryUse::Upload,
		    "light-list-" + slot);
		frame.materials = m_pDeviceContext->CreateBuffer(
		    kInitialMaterialParameters * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		    MemoryUse::Upload, "material-parameters-" + slot);

		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_frameSetLayout;
		if (vkAllocateDescriptorSets(device, &allocInfo, &frame.frameSet) != VK_SUCCESS)
			throw std::runtime_error("failed to allocate a frame descriptor set");
		m_pDeviceContext->SetDebugName(frame.frameSet, "frame-set-" + slot);
		WriteFrameSet(frame);
	}
}


void Renderer::WriteFrameSet(const FrameData &frame)
{
	const VkDescriptorBufferInfo constants{frame.constants.handle, 0, sizeof(FrameUBO)};
	const VkDescriptorBufferInfo joints{frame.joints.handle, 0, frame.joints.size};
	const VkDescriptorBufferInfo lights{frame.lights.handle, 0, frame.lights.size};
	const VkDescriptorBufferInfo materials{frame.materials.handle, 0, frame.materials.size};
	const VkSampler sampler = m_pEnvironment->GetSampler();
	const std::array<VkDescriptorImageInfo, 3> environment{{
	    {sampler, m_pEnvironment->GetSpecularView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	    {sampler, m_pEnvironment->GetIrradianceView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	    {sampler, m_pEnvironment->GetBrdfLutView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	}};
	std::array<VkWriteDescriptorSet, 7> writes{};
	for (auto &write : writes)
	{
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = frame.frameSet;
		write.descriptorCount = 1;
	}
	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[0].pBufferInfo = &constants;
	writes[1].dstBinding = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[1].pBufferInfo = &joints;
	writes[2].dstBinding = 2;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[2].pBufferInfo = &lights;
	writes[3].dstBinding = 3;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[3].pBufferInfo = &materials;
	for (std::uint32_t binding = 4; binding < 7; ++binding)
	{
		writes[binding].dstBinding = binding;
		writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[binding].pImageInfo = &environment[binding - 4];
	}
	vkUpdateDescriptorSets(
	    m_pDeviceContext->GetLogicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0,
	    nullptr);
}


void Renderer::WriteOutputSet()
{
	const VkDescriptorImageInfo image{
	    m_outputSampler, m_targets.hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_outputSet;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.pImageInfo = &image;
	vkUpdateDescriptorSets(m_pDeviceContext->GetLogicalDevice(), 1, &write, 0, nullptr);
}


void Renderer::CreatePipelines()
{
	RenderState mesh;
	mesh.depthCompare = VK_COMPARE_OP_GREATER;
	mesh.colorFormat = kHdrFormat;
	mesh.depthFormat = m_depthFormat;
	mesh.samples = m_pDeviceContext->GetMsaaSamples();
	// Every scene program is built against the mesh layout and this state, by the material cache:
	// the default program here, the ones materials name when a material first draws.
	m_pMaterials->Init(m_materialSetLayout, m_meshLayout, mesh);

	// The sky is drawn after the surfaces, at the far end of the reversed-Z range, where nothing
	// covered the depth buffer's clear; it writes no depth, so the lines drawn after it are tested
	// against the surfaces alone.
	RenderState sky = mesh;
	sky.vertices = VertexLayout::None;
	sky.cullMode = VK_CULL_MODE_NONE;
	sky.depthWrite = false;
	sky.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
	m_skyPipeline = m_pPipelines->Get({"sky", sky, m_meshLayout});

	// Lines are blended over the opaque surfaces and write no depth, so a line through a surface
	// does not hide the half behind it from the next line; the overlay variant is not tested.
	RenderState line = mesh;
	line.vertices = VertexLayout::Line;
	line.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	line.cullMode = VK_CULL_MODE_NONE;
	line.depthWrite = false;
	line.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
	line.blend = BlendMode::Alpha;
	m_linePipeline = m_pPipelines->Get({"debug_line", line, m_lineLayout});
	line.depthTest = false;
	m_lineOverlayPipeline = m_pPipelines->Get({"debug_line", line, m_lineLayout});

	RenderState output;
	output.vertices = VertexLayout::None;
	output.cullMode = VK_CULL_MODE_NONE;
	output.depthTest = false;
	output.depthWrite = false;
	output.colorFormat = m_pSwapchain->GetImageFormat();
	m_outputPipeline = m_pPipelines->Get({"output", output, m_outputLayout});
}


void Renderer::CreateRenderFinishedSemaphores()
{
	m_renderFinished.resize(m_pSwapchain->GetImageCount(), VK_NULL_HANDLE);
	m_imagesInFlight.assign(m_pSwapchain->GetImageCount(), VK_NULL_HANDLE);
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (size_t i = 0; i < m_renderFinished.size(); ++i)
	{
		if (vkCreateSemaphore(
		        m_pDeviceContext->GetLogicalDevice(), &semaphoreInfo, nullptr, &m_renderFinished[i]) !=
		    VK_SUCCESS)
			throw std::runtime_error("failed to create render-finished semaphore");
		m_pDeviceContext->SetDebugName(m_renderFinished[i], "render-finished-" + std::to_string(i));
	}
}


void Renderer::DestroyRenderFinishedSemaphores()
{
	for (const VkSemaphore semaphore : m_renderFinished)
		vkDestroySemaphore(m_pDeviceContext->GetLogicalDevice(), semaphore, nullptr);
	m_renderFinished.clear();
}


void Renderer::RecreateSwapchain()
{
	check_vk_result(m_pDeviceContext->WaitIdle());
	const VkFormat format = m_pSwapchain->GetImageFormat();
	m_pSwapchain->Recreate();
	// The pipelines were built for the swapchain's format, and the surface is the same one, so
	// the format is too; a surface that changed its mind would need them rebuilt.
	if (m_pSwapchain->GetImageFormat() != format)
		throw std::runtime_error("the swapchain's format changed when it was recreated");
	DestroyTargets();
	CreateTargets();
	WriteOutputSet();
	DestroyRenderFinishedSemaphores();
	CreateRenderFinishedSemaphores();
	m_pWindow->HasBeenResized(false);
}


void Renderer::UpdateEnvironment(const entt::registry &sceneRegistry, bool sceneReplaced)
{
	const SkyComponent *pSky = FindSky(sceneRegistry);
	std::optional<SkyComponent> sky;
	if (pSky != nullptr)
		sky = *pSky;
	// A replaced scene is rebaked even when its sky is the same, so a reload proves the whole path:
	// a jump to another system loads another sky anyway.
	if (!sceneReplaced && sky == m_bakedSky)
		return;
	m_pEnvironment->Bake(pSky);
	m_bakedSky = sky;
	// The bake waited for the queue, so neither slot's set is read by a submission in flight.
	for (const FrameData &frame : m_frames)
		WriteFrameSet(frame);
}


void Renderer::EnsureCapacity(
    FrameData &frame, Buffer &buffer, VkDeviceSize bytes, VkBufferUsageFlags usage, const char *name)
{
	if (bytes <= buffer.size)
		return;
	// The slot's fence has been waited on, so no submission in flight reads this buffer or the
	// frame set that points at it; the set is rewritten before this frame records against it.
	VkDeviceSize size = std::max<VkDeviceSize>(buffer.size, 1);
	while (size < bytes)
		size *= 2;
	m_pDeviceContext->DestroyBuffer(buffer);
	buffer = m_pDeviceContext->CreateBuffer(
	    size, usage, MemoryUse::Upload,
	    std::string{name} + "-" + std::to_string(m_currentFrame) + "-" + std::to_string(size));
	WriteFrameSet(frame);
}


void Renderer::WriteFrame(
    FrameData &frame, const entt::registry &sceneRegistry, const CameraView &camera,
    const std::vector<SceneDrawCommand> &draws, std::vector<uint32_t> &jointOffsets)
{
	FrameUBO constants{};
	constants.view = RenderViewMatrix(camera);
	const VkExtent2D extent = m_pSwapchain->GetExtents();
	constants.projection = ReversedInfinitePerspective(
	    camera.verticalFov, static_cast<float>(extent.width) / static_cast<float>(extent.height),
	    camera.nearPlane);
	const float pixelAngle =
	    2.0f * std::tan(camera.verticalFov * 0.5f) / static_cast<float>(std::max(extent.height, 1u));
	constants.environment = glm::vec4(m_pEnvironment->GetMaxSpecularLevel(), pixelAngle, 0.0f, 0.0f);
	if (const SkyComponent *pSky = FindSky(sceneRegistry))
		constants.sky = PackSky(*pSky);

	std::vector<GpuLight> lights;
	for (const entt::entity entity : sceneRegistry.view<const LightComponent>())
	{
		const auto &light = sceneRegistry.get<LightComponent>(entity);
		if (!light.active)
			continue;
		// Collected camera-relative, like every other position the GPU sees.
		glm::dvec3 position{0.0};
		if (const auto *worldTransform = sceneRegistry.try_get<WorldTransformComponent>(entity))
			position = glm::dvec3(worldTransform->matrix[3]);
		else if (const auto *transform = sceneRegistry.try_get<TransformComponent>(entity))
			position = transform->translation;
		lights.push_back(
		    {glm::vec4(ToRenderSpace(position, camera.position), 1.0f),
		     glm::vec4(light.color, light.intensity)});
	}
	constants.counts.x = static_cast<uint32_t>(lights.size());

	// The scene loader allows one sun.
	for (const auto [entity, sun] : sceneRegistry.view<const SunComponent>().each())
	{
		glm::dvec3 position{0.0};
		if (const auto *worldTransform = sceneRegistry.try_get<WorldTransformComponent>(entity))
			position = glm::dvec3(worldTransform->matrix[3]);
		// The direction is taken in double precision: the sun is a hundred million kilometres
		// away, and its own offset is too large for a float to subtract from.
		const glm::dvec3 towards = position - camera.position;
		const double distance = glm::length(towards);
		if (distance > 0.0)
		{
			constants.sunDirection = glm::vec4(glm::vec3{towards / distance}, 1.0f);
			constants.sunColorIntensity = glm::vec4(sun.color, sun.intensity);
			const auto *placement = sceneRegistry.try_get<PlacementComponent>(entity);
			const double radius = placement != nullptr && placement->radius > 0.0
			                          ? std::asin(std::min(placement->radius / distance, 1.0))
			                          : kDefaultSunAngularRadius;
			const double solidAngle = 2.0 * std::numbers::pi * (1.0 - std::cos(radius));
			constants.sunDisc = glm::vec4(
			    static_cast<float>(std::cos(radius)), static_cast<float>(radius),
			    std::min(static_cast<float>(sun.intensity / solidAngle), kMaxSunRadiance), 0.0f);
		}
	}
	EnsureCapacity(
	    frame, frame.lights, lights.size() * sizeof(GpuLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	    "light-list");
	if (!lights.empty())
		m_pDeviceContext->WriteBuffer(frame.lights, lights.data(), lights.size() * sizeof(GpuLight));
	m_pDeviceContext->WriteBuffer(frame.constants, &constants, sizeof(constants));

	jointOffsets.assign(draws.size(), 0);
	std::vector<glm::mat4> palette;
	for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
	{
		const auto *skeleton = draws[drawIndex].skeleton;
		if (skeleton == nullptr || skeleton->jointMatrices.empty())
			continue;
		if (palette.size() > std::numeric_limits<uint32_t>::max() - skeleton->jointMatrices.size())
			throw std::runtime_error("Scene skin palette exceeds the Vulkan draw offset range");
		jointOffsets[drawIndex] = static_cast<uint32_t>(palette.size());
		palette.insert(palette.end(), skeleton->jointMatrices.begin(), skeleton->jointMatrices.end());
	}
	if (palette.empty())
		palette.emplace_back(1.0f);
	EnsureCapacity(
	    frame, frame.joints, palette.size() * sizeof(glm::mat4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	    "joint-palette");
	m_pDeviceContext->WriteBuffer(frame.joints, palette.data(), palette.size() * sizeof(glm::mat4));
}


uint32_t Renderer::WriteLines(FrameData &frame, const DebugDraw &debugDraw, const glm::dvec3 &camera)
{
	const uint32_t tested = debugDraw.Build(camera, m_lineScratch);
	m_lineVertices = static_cast<uint32_t>(m_lineScratch.size());
	if (m_lineScratch.empty())
		return 0;
	EnsureCapacity(
	    frame, frame.lines, m_lineScratch.size() * sizeof(LineVertex),
	    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "debug-lines");
	m_pDeviceContext->WriteBuffer(
	    frame.lines, m_lineScratch.data(), m_lineScratch.size() * sizeof(LineVertex));
	return tested;
}


void Renderer::WriteMaterials(FrameData &frame, const std::vector<SceneDrawCommand> &draws, double time)
{
	m_drawMaterials.assign(draws.size(), nullptr);
	for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
		if (draws[drawIndex].material != nullptr && !draws[drawIndex].material->empty())
			m_drawMaterials[drawIndex] =
			    m_pMaterials->Resolve(*draws[drawIndex].material, time, m_materialParameters);
	if (m_materialParameters.size() > std::numeric_limits<uint32_t>::max())
		throw std::runtime_error("The frame's material parameters exceed the Vulkan draw offset range");
	const VkDeviceSize bytes = m_materialParameters.size() * sizeof(glm::vec4);
	EnsureCapacity(
	    frame, frame.materials, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "material-parameters");
	if (bytes != 0)
		m_pDeviceContext->WriteBuffer(frame.materials, m_materialParameters.data(), bytes);
}


bool Renderer::Render(
    Scene &scene, const SceneRenderSystem &renderSystem, const DebugDraw &debugDraw,
    ImGuiRenderer &imguiRenderer, double presentationTime)
{
	FrameData &frame = m_frames[m_currentFrame];
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
	// The slot's last frame is done, and with it everything retired long enough ago to free.
	m_pMaterials->BeginFrame(m_frameNumber, m_materialParameters);

	uint32_t imageIndex = 0;
	const VkResult acquired = vkAcquireNextImageKHR(
	    device, m_pSwapchain->GetVkSwapchainHandle(), UINT64_MAX, frame.imageAvailable, VK_NULL_HANDLE,
	    &imageIndex);
	if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
	{
		RecreateSwapchain();
		return true;
	}
	if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("failed to acquire a swapchain image");
	// Present reads this index, and it is the only writer: a recreate can change the image count
	// behind it, and the next acquire replaces it before the next present looks at it.
	m_presentImageIndex = imageIndex;
	if (m_imagesInFlight[imageIndex] != VK_NULL_HANDLE)
		vkWaitForFences(device, 1, &m_imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
	m_imagesInFlight[imageIndex] = frame.inFlight;

	// A model's materials live under the address of the Model, and a scene reload destroys models
	// whose addresses the next allocation reuses. The scene says when that happened. Freeing what
	// is left needs the device idle, because the frame in flight may be reading a set from a pool
	// that is about to go; a reload is rare, so stalling is the right trade against retiring pools
	// behind a fence that cannot be named. Named materials are keyed on their paths, which a reload
	// does not invalidate: the ones the new scene still draws are kept, reloads in flight and all,
	// and the rest are released once this frame has resolved the new scene's.
	const bool sceneReplaced = m_sceneGeneration && *m_sceneGeneration != scene.Generation();
	if (sceneReplaced)
	{
		check_vk_result(m_pDeviceContext->WaitIdle());
		m_pMaterials->Forget();
	}
	m_sceneGeneration = scene.Generation();
	UpdateEnvironment(scene.Registry(), sceneReplaced);

	const CameraView camera = ActiveCameraView(scene.Registry());
	const auto draws = renderSystem.Collect(scene.Registry(), camera.position);
	std::vector<uint32_t> jointOffsets;
	WriteFrame(frame, scene.Registry(), camera, draws, jointOffsets);
	WriteMaterials(frame, draws, presentationTime);
	if (sceneReplaced)
		m_pMaterials->ReleaseUnresolved();
	m_testedLineVertices = WriteLines(frame, debugDraw, camera.position);
	Record(frame, imageIndex, draws, jointOffsets, imguiRenderer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	// The first use of the swapchain image waits at the colour-output stage (PassBarriers'
	// source scope for Present), so the acquire's semaphore is waited on there.
	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &frame.imageAvailable;
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &frame.commandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &m_renderFinished[imageIndex];

	vkResetFences(device, 1, &frame.inFlight);
	if (vkQueueSubmit(m_pDeviceContext->GetGraphicsQueue(), 1, &submitInfo, frame.inFlight) !=
	    VK_SUCCESS)
		throw std::runtime_error("failed to submit the frame's command buffer");

	return Present();
}


bool Renderer::Present()
{
	const uint32_t imageIndex = m_presentImageIndex;
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &m_renderFinished[imageIndex];
	const VkSwapchainKHR swapchain = m_pSwapchain->GetVkSwapchainHandle();
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.pImageIndices = &imageIndex;

	const VkResult result = vkQueuePresentKHR(m_pDeviceContext->GetPresentQueue(), &presentInfo);

	// Out of date is not one answer either side of a submit. Render's acquire returns early on it
	// with the frame slot untouched, because nothing was queued; the frame has been queued here,
	// so the slot is spent whether or not the swapchain survives the present, and it moves.
	const bool outOfDate = result == VK_ERROR_OUT_OF_DATE_KHR || m_pWindow->HasBeenResized();
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR)
		throw std::runtime_error("failed to present swap chain image");

	m_currentFrame = (m_currentFrame + 1) % kFramesInFlight;
	++m_frameNumber;
	if (outOfDate)
	{
		RecreateSwapchain();
		return true;
	}
	return false;
}


void Renderer::Record(
    const FrameData &frame, uint32_t imageIndex, const std::vector<SceneDrawCommand> &draws,
    const std::vector<uint32_t> &jointOffsets, const ImGuiRenderer &imguiRenderer)
{
	const VkCommandBuffer commandBuffer = frame.commandBuffer;
	if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
		throw std::runtime_error("failed to reset command buffer");
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		throw std::runtime_error("failed to begin recording command buffer");

	// A message raised while a command executes names the frame region it came from, which is
	// the difference between "a draw in frame N reads a freed buffer" and a usable bug report.
	// Labels are commands, so they go inside the recording.
	m_pDeviceContext->BeginDebugLabel(commandBuffer, "frame-image-" + std::to_string(imageIndex));

	// This frame's images, in the pass list's resource order.
	std::vector<BoundResource> bound(m_passes.ResourceCount());
	bound[m_hdrMsaaResource.index] = {m_targets.hdrMsaa.handle, VK_IMAGE_ASPECT_COLOR_BIT};
	bound[m_depthResource.index] = {m_targets.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT};
	bound[m_hdrResource.index] = {m_targets.hdr.handle, VK_IMAGE_ASPECT_COLOR_BIT};
	bound[m_swapchainResource.index] = {m_pSwapchain->GetImage(imageIndex), VK_IMAGE_ASPECT_COLOR_BIT};

	for (const PlannedPass &planned : m_plan.passes)
	{
		RecordBarriers(commandBuffer, planned.barriers, bound);
		m_pDeviceContext->BeginDebugLabel(commandBuffer, std::string{m_passes.PassName(planned.pass)});
		if (planned.pass == m_opaquePass)
			RecordOpaque(frame, commandBuffer, draws, jointOffsets);
		else if (planned.pass == m_outputPass)
			RecordOutput(commandBuffer, imageIndex, imguiRenderer);
		m_pDeviceContext->EndDebugLabel(commandBuffer);
	}
	RecordBarriers(commandBuffer, m_plan.final, bound);

	m_pDeviceContext->EndDebugLabel(commandBuffer);
	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to record command buffer");
}


void Renderer::SetViewport(VkCommandBuffer commandBuffer) const
{
	const VkViewport viewport{0.0f,
	                          0.0f,
	                          static_cast<float>(m_targets.extent.width),
	                          static_cast<float>(m_targets.extent.height),
	                          0.0f,
	                          1.0f};
	const VkRect2D scissor{{0, 0}, m_targets.extent};
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}


void Renderer::RecordOpaque(
    const FrameData &frame, VkCommandBuffer commandBuffer, const std::vector<SceneDrawCommand> &draws,
    const std::vector<uint32_t> &jointOffsets)
{
	VkRenderingAttachmentInfo color{};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = m_targets.hdrMsaaView;
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
	color.resolveImageView = m_targets.hdrView;
	color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	color.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	color.clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

	VkRenderingAttachmentInfo depth{};
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = m_targets.depthView;
	depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	// Reversed-Z: the far end of the infinite frustum is depth 0.
	depth.clearValue.depthStencil = {0.0f, 0};

	VkRenderingInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea = {{0, 0}, m_targets.extent};
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;
	rendering.pDepthAttachment = &depth;

	vkCmdBeginRendering(commandBuffer, &rendering);
	SetViewport(commandBuffer);
	// Every scene program shares the mesh layout, so set 0 stays bound across pipeline changes.
	vkCmdBindDescriptorSets(
	    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshLayout, 0, 1, &frame.frameSet, 0,
	    nullptr);
	VkPipeline boundPipeline = VK_NULL_HANDLE;
	for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
	{
		const SceneDrawCommand &draw = draws[drawIndex];
		if (draw.model == nullptr)
			continue;
		const VkBuffer vertexBuffer = draw.model->m_vertexBuffer.handle;
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &offset);
		vkCmdBindIndexBuffer(commandBuffer, draw.model->m_indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
		const MaterialBinding *pNamed = m_drawMaterials[drawIndex];
		DrawPushConstants pushConstants{};
		pushConstants.model = draw.renderTransform;
		pushConstants.jointOffset = jointOffsets[drawIndex];
		pushConstants.skinned =
		    draw.skeleton != nullptr && !draw.skeleton->jointMatrices.empty() ? 1u : 0u;
		// A model's own material is the default program at its defaults, the frame's first block.
		pushConstants.materialOffset = pNamed != nullptr ? pNamed->parameterOffset : 0u;
		vkCmdPushConstants(
		    commandBuffer, m_meshLayout, kDrawStages, 0, sizeof(DrawPushConstants), &pushConstants);
		const auto &primitives = draw.model->m_primitives;
		for (std::size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex)
		{
			const MaterialBinding material =
			    pNamed != nullptr ? *pNamed : m_pMaterials->Ensure(*draw.model, primitiveIndex);
			if (material.pipeline != boundPipeline)
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipeline);
				boundPipeline = material.pipeline;
			}
			vkCmdBindDescriptorSets(
			    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshLayout, 1, 1, &material.set, 0,
			    nullptr);
			vkCmdDrawIndexed(
			    commandBuffer, primitives[primitiveIndex].indexCount, 1,
			    primitives[primitiveIndex].firstIndex, 0, 0);
		}
	}

	m_pDeviceContext->BeginDebugLabel(commandBuffer, "sky");
	// The sky program shares the mesh layout, so the frame set is still bound.
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline);
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);
	m_pDeviceContext->EndDebugLabel(commandBuffer);

	if (m_lineVertices != 0)
	{
		const VkDeviceSize offset = 0;
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, &frame.lines.handle, &offset);
		vkCmdBindDescriptorSets(
		    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lineLayout, 0, 1, &frame.frameSet, 0,
		    nullptr);
		if (m_testedLineVertices != 0)
		{
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
			vkCmdDraw(commandBuffer, m_testedLineVertices, 1, 0, 0);
		}
		if (m_lineVertices > m_testedLineVertices)
		{
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lineOverlayPipeline);
			vkCmdDraw(commandBuffer, m_lineVertices - m_testedLineVertices, 1, m_testedLineVertices, 0);
		}
	}
	vkCmdEndRendering(commandBuffer);
}


void Renderer::RecordOutput(
    VkCommandBuffer commandBuffer, uint32_t imageIndex, const ImGuiRenderer &imguiRenderer)
{
	VkRenderingAttachmentInfo color{};
	color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	color.imageView = m_pSwapchain->GetImageView(imageIndex);
	color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	// The triangle covers every pixel, so what was there before does not matter.
	color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea = {{0, 0}, m_targets.extent};
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachments = &color;

	vkCmdBeginRendering(commandBuffer, &rendering);
	SetViewport(commandBuffer);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outputPipeline);
	vkCmdBindDescriptorSets(
	    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outputLayout, 0, 1, &m_outputSet, 0, nullptr);
	const OutputPushConstants output{m_exposure, m_pSwapchain->EncodesSrgb() ? 0u : 1u};
	vkCmdPushConstants(
	    commandBuffer, m_outputLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(output), &output);
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);
	// The UI goes over the scene after exposure, single-sampled, in the swapchain's format.
	imguiRenderer.RenderDrawData(commandBuffer);
	vkCmdEndRendering(commandBuffer);
}
} // namespace Jettison::Render
