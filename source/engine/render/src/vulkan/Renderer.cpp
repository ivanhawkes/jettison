#include "vulkan/Renderer.h"

#include <tracy/TracyVulkan.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <jettison/core/Log.h>
#include <jettison/render/FrameData.h>
#include <jettison/scene/Scene.h>

#include "vulkan/Environment.h"
#include "vulkan/GpuProfiler.h"
#include "vulkan/MaterialCache.h"
#include "vulkan/ModelCache.h"
#include "vulkan/PipelineCache.h"
#include "vulkan/imgui/ImGuiRenderer.h"

#include "vulkan/PassBarriers.h"

namespace Jettison::Render
{
namespace
{
// assets/shaders/include/draw.glsl. What differs between the instances of a draw is in the instance
// buffer (GpuInstance).
struct DrawPushConstants
{
	uint32_t materialOffset{};
	uint32_t shadowCascade{};
};
static_assert(sizeof(DrawPushConstants) == 8);
constexpr VkShaderStageFlags kDrawStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

struct OutputPushConstants
{
	float exposure{1.0f};
	uint32_t encodeSrgb{0};
};

constexpr Core::Category kRender{"render"};

// Room for this many lights before the first frame that has more grows the list.
constexpr VkDeviceSize kInitialLights = 16;
// The cluster programs' workgroup, as cluster_bounds.comp and cluster_lights.comp declare it.
constexpr uint32_t kClusterGroupSize = 64;
// And for this many material parameters, which are vec4s.
constexpr VkDeviceSize kInitialMaterialParameters = 64;
// And for this many instances, the view's and the cascades' together.
constexpr VkDeviceSize kInitialInstances = 256;

// What the sky draws the sun as when the scene does not say how large it is: the Sun from 1 AU.
constexpr double kDefaultSunAngularRadius = 0.004652;
// The shadow pass's slope-scaled depth bias, in the depth one texel's slope covers: negative, since
// the maps are reversed and away from the sun is towards 0. With the lighting include's normal offset
// it keeps a lit surface from shadowing itself.
constexpr float kShadowSlopeBias = -2.0f;

// The brightest the sun's disc is drawn. Its true radiance, irradiance over the half-degree disc's
// solid angle, is tens of thousands of times its intensity and would overflow the half-float HDR
// target; past this the tonemapper shows white either way.
constexpr float kMaxSunRadiance = 30000.0f;

const Scene::SkyComponent *FindSky(const entt::registry &sceneRegistry)
{
	// The scene loader allows one.
	const auto view = sceneRegistry.view<const Scene::SkyComponent>();
	return view.empty() ? nullptr : &view.get<const Scene::SkyComponent>(view.front());
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
	m_shadowFormat = m_pDeviceContext->FindShadowFormat();
	m_pPipelines = std::make_unique<PipelineCache>(m_pDeviceContext, m_assets);
	m_pMaterials = std::make_unique<MaterialCache>(
	    m_pDeviceContext, m_assets, m_resources, *m_pPipelines, kFramesInFlight);
	m_pModels =
	    std::make_unique<ModelCache>(m_pDeviceContext, m_resources, *m_pMaterials, kFramesInFlight);

	CreateLayouts();
	CreatePassList();
	std::vector<std::string> passNames;
	passNames.reserve(m_passes.PassCount());
	for (std::uint32_t pass = 0; pass < m_passes.PassCount(); ++pass)
		passNames.emplace_back(m_passes.PassName(pass));
	m_pProfiler =
	    std::make_unique<GpuProfiler>(m_pDeviceContext, kFramesInFlight, std::move(passNames));
	m_pProfiler->Init();
	CreateTargets();
	CreateClusterBuffers();
	CreateShadowMap();
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
		m_pDeviceContext->DestroyBuffer(frame.instances);
		m_pDeviceContext->DestroyBuffer(frame.gridReadback);
		m_pDeviceContext->DestroyBuffer(frame.indexReadback);
		frame = {};
	}
	DestroyClusterBuffers();
	DestroyShadowMap();
	DestroyTargets();
	if (m_pProfiler)
		m_pProfiler->Destroy();
	if (m_pEnvironment)
		m_pEnvironment->Destroy();
	m_bakedSky.reset();
	// Before the materials: a model going takes its own materials with it.
	if (m_pModels)
		m_pModels->Destroy();
	if (m_pMaterials)
		m_pMaterials->Destroy();
	if (m_pPipelines)
		m_pPipelines->Destroy();
	m_drawMaterials.clear();
	m_outputPipeline = VK_NULL_HANDLE;
	m_skyPipeline = VK_NULL_HANDLE;
	m_linePipeline = VK_NULL_HANDLE;
	m_lineOverlayPipeline = VK_NULL_HANDLE;
	m_clusterBoundsPipeline = VK_NULL_HANDLE;
	m_clusterLightsPipeline = VK_NULL_HANDLE;
	m_shadowPipeline = VK_NULL_HANDLE;
	m_prepassPipeline = VK_NULL_HANDLE;

	vkDestroySampler(device, m_outputSampler, nullptr);
	vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
	vkDestroyPipelineLayout(device, m_clusterLayout, nullptr);
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
	m_clusterLayout = VK_NULL_HANDLE;
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

	// assets/shaders/include/frame.glsl, the environment's bindings in lighting.glsl, the cluster
	// grid's, which the cluster programs write and lighting.glsl reads, the sun's shadow map, and the
	// instances mesh_vertex.glsl reads.
	constexpr VkShaderStageFlags kFragmentCompute =
	    VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
	const std::array<VkDescriptorSetLayoutBinding, 12> frameBindings{{
	    {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
	     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
	     nullptr},
	    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
	    {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kFragmentCompute, nullptr},
	    {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	    {8, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kFragmentCompute, nullptr},
	    {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, kFragmentCompute, nullptr},
	    {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
	    {11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
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

	// The cluster programs read the frame set and nothing else.
	VkPipelineLayoutCreateInfo clusterInfo{};
	clusterInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	clusterInfo.setLayoutCount = 1;
	clusterInfo.pSetLayouts = &m_frameSetLayout;
	if (vkCreatePipelineLayout(device, &clusterInfo, nullptr, &m_clusterLayout) != VK_SUCCESS)
		throw std::runtime_error("failed to create the cluster pipeline layout");
	m_pDeviceContext->SetDebugName(m_clusterLayout, "cluster-pipeline-layout");

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
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7 * kFramesInFlight},
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * kFramesInFlight + 1},
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
	m_shadowMapResource = m_passes.AddImage("sun-shadows");
	m_hdrMsaaResource = m_passes.AddImage("hdr-msaa");
	m_depthResource = m_passes.AddImage("depth");
	m_hdrResource = m_passes.AddImage("hdr");
	m_swapchainResource = m_passes.AddImage("swapchain");
	m_clusterBoundsResource = m_passes.AddBuffer("cluster-bounds");
	m_clusterGridResource = m_passes.AddBuffer("cluster-grid");
	m_lightIndicesResource = m_passes.AddBuffer("light-indices");
	// Every cascade, one rendering each into its own layer, cleared and then drawn into. A scene
	// without a sun only clears them, so the plan and its barriers are the same either way.
	m_shadowPass = m_passes.AddPass(
	    "sun-shadows", PassKind::Graphics, {{m_shadowMapResource, Usage::DepthAttachment}});
	// The opaque surfaces' depth, laid down for the opaque pass to test against, when the prepass is
	// on (SetDepthPrepass); off, it records nothing and the opaque pass clears the depth itself, so
	// the plan and its barriers are the same either way.
	m_prepassPass = m_passes.AddPass(
	    "depth-prepass", PassKind::Graphics, {{m_depthResource, Usage::DepthAttachment}});
	// The index list's allocator is zeroed by a fill, so the assignment can reserve from it with
	// atomic adds; a fill is a transfer, and the plan orders it after the last frame's reads.
	m_clusterResetPass = m_passes.AddPass(
	    "cluster-reset", PassKind::Transfer, {{m_lightIndicesResource, Usage::TransferWrite}});
	// Recorded only when the bounds are stale; its barriers are recorded either way, and a buffer
	// keeps its contents between frames (render/PassList.h).
	m_clusterBoundsPass = m_passes.AddPass(
	    "cluster-bounds", PassKind::Compute, {{m_clusterBoundsResource, Usage::StorageWriteCompute}});
	m_clusterLightsPass = m_passes.AddPass(
	    "cluster-lights", PassKind::Compute,
	    {{m_clusterBoundsResource, Usage::StorageReadCompute},
	     {m_clusterGridResource, Usage::StorageWriteCompute},
	     {m_lightIndicesResource, Usage::StorageWriteCompute}});
	// The resolve is the opaque pass's: dynamic rendering resolves the multisampled target into
	// the single-sampled one as the pass ends, as a colour attachment write.
	m_opaquePass = m_passes.AddPass(
	    "opaque", PassKind::Graphics,
	    {{m_hdrMsaaResource, Usage::ColorAttachment},
	     {m_depthResource, Usage::DepthAttachment},
	     {m_hdrResource, Usage::ColorAttachment},
	     {m_shadowMapResource, Usage::SampledFragment},
	     {m_clusterGridResource, Usage::StorageReadFragment},
	     {m_lightIndicesResource, Usage::StorageReadFragment}});
	m_outputPass = m_passes.AddPass(
	    "output", PassKind::Graphics,
	    {{m_hdrResource, Usage::SampledFragment}, {m_swapchainResource, Usage::ColorAttachment}});
	// The cluster check's copy. It is in every frame's plan, checked or not, and records its copies
	// only when the check is on: the plan the gate's synchronization validation runs is then the
	// plan every run has, and the barriers it costs an unchecked frame wait on work that is done.
	m_clusterReadbackPass = m_passes.AddPass(
	    "cluster-readback", PassKind::Transfer,
	    {{m_clusterGridResource, Usage::TransferRead}, {m_lightIndicesResource, Usage::TransferRead}});
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

	// The depth is stored by the prepass, when it runs, and loaded by the opaque pass, so it is not
	// transient.
	m_targets.depth = m_pDeviceContext->CreateImage(
	    extent.width, extent.height, 1, samples, m_depthFormat, VK_IMAGE_TILING_OPTIMAL,
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, "depth");
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


void Renderer::CreateClusterBuffers()
{
	// The grid's size does not depend on the resolution, so a resize keeps these; only the bounds
	// in them go stale.
	m_clusterBounds = m_pDeviceContext->CreateBuffer(
	    VkDeviceSize{kClusterCount} * 2 * sizeof(glm::vec4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	    MemoryUse::Device, "cluster-bounds");
	m_clusterGrid = m_pDeviceContext->CreateBuffer(
	    VkDeviceSize{kClusterCount} * sizeof(glm::uvec2),
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUse::Device,
	    "cluster-grid");
	m_lightIndices = m_pDeviceContext->CreateBuffer(
	    (VkDeviceSize{kLightIndexCapacity} + 1) * sizeof(uint32_t),
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
	        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
	    MemoryUse::Device, "light-indices");
	m_clusterBoundsView.reset();
}


void Renderer::DestroyClusterBuffers()
{
	m_pDeviceContext->DestroyBuffer(m_lightIndices);
	m_pDeviceContext->DestroyBuffer(m_clusterGrid);
	m_pDeviceContext->DestroyBuffer(m_clusterBounds);
	m_clusterBoundsView.reset();
}


void Renderer::CreateShadowMap()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	m_shadowMap = m_pDeviceContext->CreateArrayImage(
	    kShadowMapSize, kShadowMapSize, kShadowCascades, m_shadowFormat,
	    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "sun-shadows");
	m_shadowArrayView = m_pDeviceContext->CreateImageView(
	    m_shadowMap.handle, m_shadowFormat, VK_IMAGE_VIEW_TYPE_2D_ARRAY,
	    {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, kShadowCascades});
	m_pDeviceContext->SetDebugName(m_shadowArrayView, "sun-shadows-view");
	for (std::uint32_t cascade = 0; cascade < kShadowCascades; ++cascade)
	{
		m_shadowLayerViews[cascade] = m_pDeviceContext->CreateImageView(
		    m_shadowMap.handle, m_shadowFormat, VK_IMAGE_VIEW_TYPE_2D,
		    {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, cascade, 1});
		m_pDeviceContext->SetDebugName(
		    m_shadowLayerViews[cascade], "sun-shadows-cascade-" + std::to_string(cascade));
	}

	// A filtered comparison: each tap is the share of its four texels that the receiver is at least as
	// near the sun as, which is what the reversed maps hold the nearest of. Outside a map the border
	// is depth 0, the far side, so nothing there shadows; the fit keeps every lookup inside anyway.
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	samplerInfo.compareEnable = VK_TRUE;
	samplerInfo.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
	samplerInfo.maxLod = 0.0f;
	if (vkCreateSampler(device, &samplerInfo, nullptr, &m_shadowSampler) != VK_SUCCESS)
		throw std::runtime_error("failed to create the shadow sampler");
	m_pDeviceContext->SetDebugName(m_shadowSampler, "sun-shadows-sampler");
}


void Renderer::DestroyShadowMap()
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	vkDestroySampler(device, m_shadowSampler, nullptr);
	for (VkImageView &view : m_shadowLayerViews)
	{
		vkDestroyImageView(device, view, nullptr);
		view = VK_NULL_HANDLE;
	}
	vkDestroyImageView(device, m_shadowArrayView, nullptr);
	m_pDeviceContext->DestroyImage(m_shadowMap);
	m_shadowSampler = VK_NULL_HANDLE;
	m_shadowArrayView = VK_NULL_HANDLE;
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
		frame.instances = m_pDeviceContext->CreateBuffer(
		    kInitialInstances * sizeof(GpuInstance), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		    MemoryUse::Upload, "instances-" + slot);

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
	const VkDescriptorBufferInfo instances{frame.instances.handle, 0, frame.instances.size};
	const std::array<VkDescriptorBufferInfo, 3> clusters{{
	    {m_clusterBounds.handle, 0, m_clusterBounds.size},
	    {m_clusterGrid.handle, 0, m_clusterGrid.size},
	    {m_lightIndices.handle, 0, m_lightIndices.size},
	}};
	const VkSampler sampler = m_pEnvironment->GetSampler();
	const std::array<VkDescriptorImageInfo, 3> environment{{
	    {sampler, m_pEnvironment->GetSpecularView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	    {sampler, m_pEnvironment->GetIrradianceView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	    {sampler, m_pEnvironment->GetBrdfLutView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
	}};
	const VkDescriptorImageInfo shadows{
	    m_shadowSampler, m_shadowArrayView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	std::array<VkWriteDescriptorSet, 12> writes{};
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
	for (std::uint32_t binding = 7; binding < 10; ++binding)
	{
		writes[binding].dstBinding = binding;
		writes[binding].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[binding].pBufferInfo = &clusters[binding - 7];
	}
	writes[10].dstBinding = 10;
	writes[10].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[10].pImageInfo = &shadows;
	writes[11].dstBinding = 11;
	writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[11].pBufferInfo = &instances;
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
	// Greater or equal rather than greater, so a fragment at the depth the prepass laid down for it
	// passes: with the prepass on, that is the only one that does.
	RenderState mesh;
	mesh.depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
	mesh.colorFormat = kHdrFormat;
	mesh.depthFormat = m_depthFormat;
	mesh.samples = m_pDeviceContext->GetMsaaSamples();
	// Every scene program is built against the mesh layout and this state, by the material cache:
	// the default program here, the ones materials name when a material first draws.
	m_pMaterials->Init(m_materialSetLayout, m_meshLayout, mesh);
	m_pModels->Init();

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

	// The sun's cascades: depth only, every draw's vertex stage, reversed as the main depth buffer
	// is. Both faces are drawn, so an open mesh casts too, and depth is clamped rather than clipped,
	// so what lies between the sun and a cascade's map is drawn at the map's sun side and shadows it.
	RenderState shadow;
	shadow.cullMode = VK_CULL_MODE_NONE;
	shadow.depthCompare = VK_COMPARE_OP_GREATER;
	shadow.depthClamp = true;
	shadow.depthBiasSlope = kShadowSlopeBias;
	shadow.depthFormat = m_shadowFormat;
	m_shadowPipeline = m_pPipelines->Get({"shadow_depth", shadow, m_meshLayout});

	// The depth prepass: the opaque pass's depth, the same positions (mesh.vert's invariant
	// gl_Position), with no colour.
	RenderState prepass;
	prepass.depthCompare = VK_COMPARE_OP_GREATER;
	prepass.depthFormat = m_depthFormat;
	prepass.samples = m_pDeviceContext->GetMsaaSamples();
	m_prepassPipeline = m_pPipelines->Get({"depth_prepass", prepass, m_meshLayout});

	m_clusterBoundsPipeline = m_pPipelines->GetCompute("cluster_bounds", m_clusterLayout);
	m_clusterLightsPipeline = m_pPipelines->GetCompute("cluster_lights", m_clusterLayout);

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
	const Scene::SkyComponent *pSky = FindSky(sceneRegistry);
	std::optional<Scene::SkyComponent> sky;
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
	if (const Scene::SkyComponent *pSky = FindSky(sceneRegistry))
		constants.sky = PackSky(*pSky);

	// The grid spans the near plane to the cluster range, which has to be past it for the slices
	// to have any depth.
	const glm::vec2 viewport{static_cast<float>(extent.width), static_cast<float>(extent.height)};
	m_frameClusterView.viewport = viewport;
	m_frameClusterView.projectionScale = {constants.projection[0][0], constants.projection[1][1]};
	m_frameClusterView.nearPlane = camera.nearPlane;
	m_frameClusterView.range = std::max(m_clusterRange, 2.0f * camera.nearPlane);
	constants.clusterDepth =
	    glm::vec4(m_frameClusterView.nearPlane, m_frameClusterView.range, 0.0f, 0.0f);
	constants.clusterTile = glm::vec4(ClusterTileSize(viewport), viewport);

	m_frameStats = {};
	m_frameFrustum = ViewFrustum(camera, double{viewport.x} / double{std::max(viewport.y, 1.0f)});
	m_frameLods = MakeLodSelection(camera, double{viewport.y}, double{m_lodThreshold});
	SelectLods(draws);
	WriteLights(frame, sceneRegistry, camera);
	constants.counts.x = static_cast<uint32_t>(m_lightScratch.size());
	constants.counts.y = static_cast<uint32_t>(m_debugView);
	if (m_checkClusters)
	{
		frame.checkView = m_frameClusterView;
		frame.checkViewMatrix = glm::mat3(constants.view);
		frame.checkLights = m_lightScratch;
	}

	// The scene loader allows one sun.
	m_frameShadows = false;
	for (const auto [entity, sun] : sceneRegistry.view<const Scene::SunComponent>().each())
	{
		glm::dvec3 position{0.0};
		if (const auto *worldTransform = sceneRegistry.try_get<Scene::WorldTransformComponent>(entity))
			position = glm::dvec3(worldTransform->matrix[3]);
		// The direction is taken in double precision: the sun is a hundred million kilometres
		// away, and its own offset is too large for a float to subtract from.
		const glm::dvec3 towards = position - camera.position;
		const double distance = glm::length(towards);
		if (distance > 0.0)
		{
			constants.sunDirection = glm::vec4(glm::vec3{towards / distance}, 1.0f);
			constants.sunColorIntensity = glm::vec4(sun.color, sun.intensity);
			const auto *placement = sceneRegistry.try_get<Scene::PlacementComponent>(entity);
			const double radius = placement != nullptr && placement->radius > 0.0
			                          ? std::asin(std::min(placement->radius / distance, 1.0))
			                          : kDefaultSunAngularRadius;
			const double solidAngle = 2.0 * std::numbers::pi * (1.0 - std::cos(radius));
			constants.sunDisc = glm::vec4(
			    static_cast<float>(std::cos(radius)), static_cast<float>(radius),
			    std::min(static_cast<float>(sun.intensity / solidAngle), kMaxSunRadiance), 0.0f);

			m_frameCascades = FitSunShadows(
			    camera, viewport.x / std::max(viewport.y, 1.0f), towards / distance, m_shadowSettings);
			for (std::uint32_t cascade = 0; cascade < kShadowCascades; ++cascade)
				constants.shadowMatrices[cascade] = m_frameCascades.cascades[cascade].matrix;
			constants.shadowSplits = m_frameCascades.Splits();
			constants.shadowTexels = m_frameCascades.TexelSizes();
			constants.counts.z = 1u;
			m_frameShadows = true;
		}
	}
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

	WriteInstances(frame, draws, jointOffsets);
}


void Renderer::SelectLods(const std::vector<SceneDrawCommand> &draws)
{
	m_drawLods.assign(draws.size(), 0);
	m_occluders.clear();
	for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
	{
		const SceneDrawCommand &draw = draws[drawIndex];
		if (draw.model == nullptr || draw.model->m_lods.empty())
			continue;
		const std::uint32_t lod = SelectLod(draw.model->m_lods, draw.scale, draw.bounds, m_frameLods);
		m_drawLods[drawIndex] = lod;
		if (draw.occluderRadius <= 0.0)
			continue;
		// The level's flat triangles dip inside the sphere they stand for by its error, so the sphere
		// that hides nothing the mesh does not is that much smaller; and a millionth smaller again, for
		// the bounds' centre, which is the box around the float vertices' and a planet's radius from
		// its true centre is a metre in float.
		const auto error = double{draw.model->m_lods[lod].error};
		m_occluders.push_back({{draw.bounds.center, draw.occluderRadius * (1.0 - error - 1e-6)}});
	}
}


void Renderer::WriteInstances(
    FrameData &frame, const std::vector<SceneDrawCommand> &draws,
    const std::vector<uint32_t> &jointOffsets)
{
	m_instanceDraws.clear();
	m_viewBatches.clear();
	for (auto &batches : m_shadowBatches)
		batches.clear();

	const auto nearest = [](const BoundingSphere &bounds)
	{ return std::max(glm::length(bounds.center) - bounds.radius, 0.0); };
	const auto triangles = [](const std::vector<DrawBatch> &batches)
	{
		std::uint64_t total = 0;
		for (const DrawBatch &batch : batches)
		{
			const ModelLod &lod = batch.key.model->m_lods[batch.key.lod];
			std::uint64_t indices = 0;
			for (std::uint32_t primitive = 0; primitive < lod.primitiveCount; ++primitive)
				indices += batch.key.model->m_primitives[lod.firstPrimitive + primitive].indexCount;
			total += indices / 3 * batch.instanceCount;
		}
		return total;
	};

	// The view: what is inside the frustum and not behind a planet. A skinned draw's pose can leave
	// its model's bounds, so it is always drawn.
	m_batchScratch.clear();
	for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
	{
		const SceneDrawCommand &draw = draws[drawIndex];
		if (draw.model == nullptr || draw.model->m_lods.empty())
			continue;
		++m_frameStats.meshes;
		const bool skinned = draw.Skinned();
		if (!skinned && !Intersects(m_frameFrustum, draw.bounds))
		{
			++m_frameStats.frustumCulled;
			continue;
		}
		if (!skinned && std::ranges::any_of(
		                    m_occluders, [&](const HorizonOccluder &occluder)
		                    { return Occludes(occluder, draw.bounds); }))
		{
			++m_frameStats.horizonCulled;
			continue;
		}
		m_batchScratch.push_back(
		    {{draw.model, m_drawLods[drawIndex], m_drawMaterials[drawIndex]},
		     static_cast<std::uint32_t>(drawIndex),
		     nearest(draw.bounds),
		     !skinned});
	}
	BuildBatches(m_batchScratch, m_viewBatches, m_instanceDraws);
	m_frameStats.viewBatches = static_cast<std::uint32_t>(m_viewBatches.size());
	m_frameStats.viewTriangles = triangles(m_viewBatches);

	// Each cascade's casters: what overlaps its map across the sun, from anywhere sunwards of its far
	// side. They are drawn at the view's level of detail, so a surface shadows itself as it is drawn.
	if (m_frameShadows)
		for (std::uint32_t cascade = 0; cascade < kShadowCascades; ++cascade)
		{
			m_batchScratch.clear();
			for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex)
			{
				const SceneDrawCommand &draw = draws[drawIndex];
				if (draw.model == nullptr || draw.model->m_lods.empty())
					continue;
				const bool skinned = draw.Skinned();
				if (!skinned && !CastsInto(m_frameCascades.cascades[cascade], draw.bounds))
					continue;
				m_batchScratch.push_back(
				    {{draw.model, m_drawLods[drawIndex], nullptr},
				     static_cast<std::uint32_t>(drawIndex),
				     nearest(draw.bounds),
				     !skinned});
			}
			m_frameStats.shadowCasters[cascade] = static_cast<std::uint32_t>(m_batchScratch.size());
			BuildBatches(m_batchScratch, m_shadowBatches[cascade], m_instanceDraws);
			m_frameStats.shadowBatches[cascade] =
			    static_cast<std::uint32_t>(m_shadowBatches[cascade].size());
			m_frameStats.shadowTriangles += triangles(m_shadowBatches[cascade]);
		}

	m_instanceScratch.resize(std::max<std::size_t>(m_instanceDraws.size(), 1));
	for (std::size_t instance = 0; instance < m_instanceDraws.size(); ++instance)
	{
		const std::uint32_t drawIndex = m_instanceDraws[instance];
		const SceneDrawCommand &draw = draws[drawIndex];
		m_instanceScratch[instance] = {
		    draw.renderTransform,
		    glm::uvec4{jointOffsets[drawIndex], draw.Skinned() ? 1u : 0u, 0u, 0u}};
	}
	const VkDeviceSize bytes = m_instanceScratch.size() * sizeof(GpuInstance);
	EnsureCapacity(frame, frame.instances, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "instances");
	m_pDeviceContext->WriteBuffer(frame.instances, m_instanceScratch.data(), bytes);
}


void Renderer::WriteLights(
    FrameData &frame, const entt::registry &sceneRegistry, const CameraView &camera)
{
	m_lightScratch.clear();
	for (const auto [entity, light] :
	     sceneRegistry.view<const Scene::LightComponent>(entt::exclude<Scene::HiddenComponent>).each())
	{
		if (!light.active)
			continue;
		// Collected camera-relative, like every other position the GPU sees. A spot points along its
		// entity's -Z, the world transform's third column without its scale.
		glm::dvec3 position{0.0};
		glm::dvec3 direction{0.0, 0.0, -1.0};
		if (const auto *worldTransform = sceneRegistry.try_get<Scene::WorldTransformComponent>(entity))
		{
			position = glm::dvec3(worldTransform->matrix[3]);
			const glm::dvec3 axis{worldTransform->matrix[2]};
			if (glm::length(axis) > 0.0)
				direction = -glm::normalize(axis);
		}
		else if (const auto *transform = sceneRegistry.try_get<Scene::TransformComponent>(entity))
			position = transform->translation;
		// The distance is taken in double precision, before anything becomes a float: a light whose
		// range ends short of the grid's far end from here reaches no cluster. Nor does one whose reach
		// is outside the view, or wholly behind a planet: every surface it could light is too.
		++m_frameStats.lights;
		const glm::dvec3 relative = position - camera.position;
		if (glm::length(relative) - double{light.range} > double{m_frameClusterView.range})
			continue;
		const BoundingSphere reach{relative, double{light.range}};
		if (!Intersects(m_frameFrustum, reach) || std::ranges::any_of(
		                                              m_occluders, [&](const HorizonOccluder &occluder)
		                                              { return Occludes(occluder, reach); }))
			continue;
		m_lightScratch.push_back(PackLight(light, glm::vec3{relative}, glm::vec3{direction}));
	}
	m_frameStats.listedLights = static_cast<std::uint32_t>(m_lightScratch.size());
	EnsureCapacity(
	    frame, frame.lights, m_lightScratch.size() * sizeof(GpuLight),
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "light-list");
	if (!m_lightScratch.empty())
		m_pDeviceContext->WriteBuffer(
		    frame.lights, m_lightScratch.data(), m_lightScratch.size() * sizeof(GpuLight));
}


void Renderer::CheckClusters(FrameData &frame)
{
	frame.checkPending = false;
	std::vector<glm::uvec2> grid(kClusterCount);
	std::vector<std::uint32_t> list(std::size_t{kLightIndexCapacity} + 1);
	m_pDeviceContext->ReadBuffer(frame.gridReadback, grid.data(), grid.size() * sizeof(glm::uvec2));
	m_pDeviceContext->ReadBuffer(frame.indexReadback, list.data(), list.size() * sizeof(std::uint32_t));
	const ClusterCheck check = CheckClusterAssignment(
	    frame.checkView, frame.checkViewMatrix, frame.checkLights, grid,
	    std::span<const std::uint32_t>{list}.subspan(1), list[0]);

	++m_clusterChecks.frames;
	m_clusterChecks.peakEntries = std::max(m_clusterChecks.peakEntries, check.entries);
	m_clusterChecks.peakClusterLights = std::max(m_clusterChecks.peakClusterLights, check.mostLights);
	if (check.litClusters != 0)
		++m_clusterChecks.litFrames;
	if (check.Agrees())
		return;
	const std::string failure =
	    check.overflowed ? std::format(
	                           "the light index list overflowed: {} entries reserved for {}", list[0],
	                           kLightIndexCapacity)
	                     : "the GPU's light assignment disagrees with the CPU's: " + check.mismatch;
	// Reported once, and counted after: a frame loop that disagrees once disagrees every frame.
	if (m_clusterChecks.failures++ == 0)
	{
		m_clusterChecks.firstFailure = failure;
		Core::Log(
		    Core::Severity::Error, kRender, "cluster check, frame {}: {}", m_frameNumber, failure);
	}
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
    const Scene::Scene &scene, const DebugDraw &debugDraw, ImGuiRenderer &imguiRenderer,
    double presentationTime)
{
	FrameData &frame = m_frames[m_currentFrame];
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);
	// The slot's last frame is done, and with it everything retired long enough ago to free.
	m_pMaterials->BeginFrame(m_frameNumber, m_materialParameters);
	m_pModels->BeginFrame(m_frameNumber);
	m_pProfiler->Collect(m_currentFrame);
	if (frame.checkPending)
		CheckClusters(frame);

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

	// Models and named materials are keyed on what the scene names, not on addresses, so a scene
	// whose contents were replaced leaves none of them stale: the ones the new contents still draw
	// are kept, loads in flight and all, and the rest are released once this frame has resolved
	// the new contents' own, into queues that wait out the frames in flight.
	const bool sceneReplaced = m_sceneGeneration && *m_sceneGeneration != scene.Generation();
	m_sceneGeneration = scene.Generation();
	UpdateEnvironment(scene.Registry(), sceneReplaced);

	const CameraView camera = ActiveCameraView(scene.Registry());
	const auto draws = CollectDraws(scene.Registry(), camera.position, *m_pModels);
	std::vector<uint32_t> jointOffsets;
	// The materials first: a draw's batch is keyed on the one it resolved to.
	WriteMaterials(frame, draws, presentationTime);
	if (sceneReplaced)
	{
		m_pModels->ReleaseUnresolved();
		m_pMaterials->ReleaseUnresolved();
	}
	WriteFrame(frame, scene.Registry(), camera, draws, jointOffsets);
	m_testedLineVertices = WriteLines(frame, debugDraw, camera.position);
	if (m_checkClusters && frame.gridReadback.handle == VK_NULL_HANDLE)
	{
		const std::string slot = std::to_string(m_currentFrame);
		frame.gridReadback = m_pDeviceContext->CreateBuffer(
		    m_clusterGrid.size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUse::Readback,
		    "cluster-grid-readback-" + slot);
		frame.indexReadback = m_pDeviceContext->CreateBuffer(
		    m_lightIndices.size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, MemoryUse::Readback,
		    "light-indices-readback-" + slot);
	}
	Record(frame, imageIndex, imguiRenderer);

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


void Renderer::Record(FrameData &frame, uint32_t imageIndex, const ImGuiRenderer &imguiRenderer)
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
	m_pProfiler->BeginFrame(commandBuffer, m_currentFrame);

	// This frame's images, in the pass list's resource order.
	std::vector<BoundResource> bound(m_passes.ResourceCount());
	bound[m_shadowMapResource.index] = {m_shadowMap.handle, VK_IMAGE_ASPECT_DEPTH_BIT};
	bound[m_hdrMsaaResource.index] = {m_targets.hdrMsaa.handle, VK_IMAGE_ASPECT_COLOR_BIT};
	bound[m_depthResource.index] = {m_targets.depth.handle, VK_IMAGE_ASPECT_DEPTH_BIT};
	bound[m_hdrResource.index] = {m_targets.hdr.handle, VK_IMAGE_ASPECT_COLOR_BIT};
	bound[m_swapchainResource.index] = {m_pSwapchain->GetImage(imageIndex), VK_IMAGE_ASPECT_COLOR_BIT};
	bound[m_clusterBoundsResource.index] = {VK_NULL_HANDLE, 0, m_clusterBounds.handle};
	bound[m_clusterGridResource.index] = {VK_NULL_HANDLE, 0, m_clusterGrid.handle};
	bound[m_lightIndicesResource.index] = {VK_NULL_HANDLE, 0, m_lightIndices.handle};

	for (const PlannedPass &planned : m_plan.passes)
	{
		RecordBarriers(commandBuffer, planned.barriers, bound);
		m_pDeviceContext->BeginDebugLabel(commandBuffer, std::string{m_passes.PassName(planned.pass)});
		// A GPU zone per declared pass, on Tracy's timeline when it is compiled in, and a timestamp
		// either side for the engine's own timings. The pass list's names are std::strings, so the
		// view's data is terminated.
		TracyVkZoneTransient(
		    static_cast<TracyVkCtx>(m_pProfiler->TracyContext()), passZone, commandBuffer,
		    m_passes.PassName(planned.pass).data(), true);
		m_pProfiler->BeginPass(commandBuffer, m_currentFrame, planned.pass);
		if (planned.pass == m_shadowPass)
			RecordShadows(frame, commandBuffer);
		else if (planned.pass == m_prepassPass)
			RecordDepthPrepass(frame, commandBuffer);
		else if (planned.pass == m_clusterResetPass)
			vkCmdFillBuffer(commandBuffer, m_lightIndices.handle, 0, sizeof(uint32_t), 0);
		else if (planned.pass == m_clusterBoundsPass)
			RecordClusterBounds(frame, commandBuffer);
		else if (planned.pass == m_clusterLightsPass)
			RecordClusterLights(frame, commandBuffer);
		else if (planned.pass == m_opaquePass)
			RecordOpaque(frame, commandBuffer);
		else if (planned.pass == m_outputPass)
			RecordOutput(commandBuffer, imageIndex, imguiRenderer);
		else if (planned.pass == m_clusterReadbackPass)
			RecordClusterReadback(frame, commandBuffer);
		m_pProfiler->EndPass(commandBuffer, m_currentFrame, planned.pass);
		m_pDeviceContext->EndDebugLabel(commandBuffer);
	}
	RecordBarriers(commandBuffer, m_plan.final, bound);

	m_pDeviceContext->EndDebugLabel(commandBuffer);
	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
		throw std::runtime_error("failed to record command buffer");
}


void Renderer::RecordDepthBatches(
    VkCommandBuffer commandBuffer, const std::vector<DrawBatch> &batches, std::uint32_t cascade) const
{
	const DrawPushConstants pushConstants{0u, cascade};
	vkCmdPushConstants(
	    commandBuffer, m_meshLayout, kDrawStages, 0, sizeof(DrawPushConstants), &pushConstants);
	const Model *boundModel = nullptr;
	for (const DrawBatch &batch : batches)
	{
		const Model &model = *batch.key.model;
		if (&model != boundModel)
		{
			const VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &model.m_vertexBuffer.handle, &offset);
			vkCmdBindIndexBuffer(commandBuffer, model.m_indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
			boundModel = &model;
		}
		const ModelLod &lod = model.m_lods[batch.key.lod];
		for (std::uint32_t index = 0; index < lod.primitiveCount; ++index)
		{
			const ModelPrimitive &primitive = model.m_primitives[lod.firstPrimitive + index];
			vkCmdDrawIndexed(
			    commandBuffer, primitive.indexCount, batch.instanceCount, primitive.firstIndex, 0,
			    batch.firstInstance);
		}
	}
}


void Renderer::RecordShadows(const FrameData &frame, VkCommandBuffer commandBuffer) const
{
	const VkExtent2D extent{kShadowMapSize, kShadowMapSize};
	const VkViewport viewport{
	    0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
	const VkRect2D scissor{{0, 0}, extent};
	for (std::uint32_t cascade = 0; cascade < kShadowCascades; ++cascade)
	{
		VkRenderingAttachmentInfo depth{};
		depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
		depth.imageView = m_shadowLayerViews[cascade];
		depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		// Reversed: 0 is the far side of the map from the sun.
		depth.clearValue.depthStencil = {0.0f, 0};

		VkRenderingInfo rendering{};
		rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
		rendering.renderArea = scissor;
		rendering.layerCount = 1;
		rendering.pDepthAttachment = &depth;

		m_pDeviceContext->BeginDebugLabel(commandBuffer, "cascade-" + std::to_string(cascade));
		vkCmdBeginRendering(commandBuffer, &rendering);
		if (m_frameShadows && !m_shadowBatches[cascade].empty())
		{
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);
			// The shadow program reads the frame set alone: the cascades' matrices, the instances and
			// the skin palette. No material set is bound, since it has no fragment stage to read one.
			vkCmdBindDescriptorSets(
			    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshLayout, 0, 1, &frame.frameSet, 0,
			    nullptr);
			RecordDepthBatches(commandBuffer, m_shadowBatches[cascade], cascade);
		}
		vkCmdEndRendering(commandBuffer);
		m_pDeviceContext->EndDebugLabel(commandBuffer);
	}
}


void Renderer::RecordDepthPrepass(const FrameData &frame, VkCommandBuffer commandBuffer) const
{
	if (!m_depthPrepass)
		return;
	VkRenderingAttachmentInfo depth{};
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = m_targets.depthView;
	depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	// Reversed-Z: the far end of the infinite frustum is depth 0.
	depth.clearValue.depthStencil = {0.0f, 0};

	VkRenderingInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea = {{0, 0}, m_targets.extent};
	rendering.layerCount = 1;
	rendering.pDepthAttachment = &depth;

	vkCmdBeginRendering(commandBuffer, &rendering);
	SetViewport(commandBuffer);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_prepassPipeline);
	vkCmdBindDescriptorSets(
	    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshLayout, 0, 1, &frame.frameSet, 0,
	    nullptr);
	RecordDepthBatches(commandBuffer, m_viewBatches, 0);
	vkCmdEndRendering(commandBuffer);
}


void Renderer::RecordClusterBounds(const FrameData &frame, VkCommandBuffer commandBuffer)
{
	if (m_clusterBoundsView == m_frameClusterView)
		return;
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_clusterBoundsPipeline);
	vkCmdBindDescriptorSets(
	    commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_clusterLayout, 0, 1, &frame.frameSet, 0,
	    nullptr);
	vkCmdDispatch(commandBuffer, (kClusterCount + kClusterGroupSize - 1) / kClusterGroupSize, 1, 1);
	// Every frame after this one reads what this dispatch writes, in submission order, and the
	// plan's barriers order their reads after it.
	m_clusterBoundsView = m_frameClusterView;
}


void Renderer::RecordClusterLights(const FrameData &frame, VkCommandBuffer commandBuffer) const
{
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_clusterLightsPipeline);
	vkCmdBindDescriptorSets(
	    commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_clusterLayout, 0, 1, &frame.frameSet, 0,
	    nullptr);
	vkCmdDispatch(commandBuffer, (kClusterCount + kClusterGroupSize - 1) / kClusterGroupSize, 1, 1);
}


void Renderer::RecordClusterReadback(FrameData &frame, VkCommandBuffer commandBuffer) const
{
	if (!m_checkClusters)
		return;
	const VkBufferCopy grid{0, 0, m_clusterGrid.size};
	vkCmdCopyBuffer(commandBuffer, m_clusterGrid.handle, frame.gridReadback.handle, 1, &grid);
	const VkBufferCopy list{0, 0, m_lightIndices.size};
	vkCmdCopyBuffer(commandBuffer, m_lightIndices.handle, frame.indexReadback.handle, 1, &list);
	// The copies made available to the host, which reads them once the slot's fence is waited on;
	// the fence alone makes device writes visible to the device, not to the host's reads.
	VkMemoryBarrier2 toHost{};
	toHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
	toHost.srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
	toHost.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	toHost.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	toHost.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
	VkDependencyInfo dependency{};
	dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependency.memoryBarrierCount = 1;
	dependency.pMemoryBarriers = &toHost;
	vkCmdPipelineBarrier2(commandBuffer, &dependency);
	frame.checkPending = true;
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


void Renderer::RecordOpaque(const FrameData &frame, VkCommandBuffer commandBuffer)
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

	// The prepass's depth when it ran, and a clear when it did not.
	VkRenderingAttachmentInfo depth{};
	depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depth.imageView = m_targets.depthView;
	depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depth.loadOp = m_depthPrepass ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
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
	const Model *boundModel = nullptr;
	// Front to back, batch by batch (render/DrawBatches.h).
	for (const DrawBatch &batch : m_viewBatches)
	{
		const Model &model = *batch.key.model;
		if (&model != boundModel)
		{
			const VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, &model.m_vertexBuffer.handle, &offset);
			vkCmdBindIndexBuffer(commandBuffer, model.m_indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);
			boundModel = &model;
		}
		const auto *pNamed = static_cast<const MaterialBinding *>(batch.key.material);
		// A model's own material is the default program at its defaults, the frame's first block.
		const DrawPushConstants pushConstants{pNamed != nullptr ? pNamed->parameterOffset : 0u, 0u};
		vkCmdPushConstants(
		    commandBuffer, m_meshLayout, kDrawStages, 0, sizeof(DrawPushConstants), &pushConstants);
		const ModelLod &lod = model.m_lods[batch.key.lod];
		for (std::uint32_t index = 0; index < lod.primitiveCount; ++index)
		{
			const std::uint32_t primitiveIndex = lod.firstPrimitive + index;
			const MaterialBinding material =
			    pNamed != nullptr
			        ? *pNamed
			        : m_pMaterials->Ensure(model, model.MaterialPrimitive(primitiveIndex));
			if (material.pipeline != boundPipeline)
			{
				vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, material.pipeline);
				boundPipeline = material.pipeline;
			}
			vkCmdBindDescriptorSets(
			    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshLayout, 1, 1, &material.set, 0,
			    nullptr);
			const ModelPrimitive &primitive = model.m_primitives[primitiveIndex];
			vkCmdDrawIndexed(
			    commandBuffer, primitive.indexCount, batch.instanceCount, primitive.firstIndex, 0,
			    batch.firstInstance);
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


const std::vector<PassTiming> &Renderer::GetPassTimings() const
{
	return m_pProfiler->Timings();
}


std::uint64_t Renderer::GetMeasuredFrames() const
{
	return m_pProfiler->MeasuredFrames();
}


void Renderer::ResetPassTimings()
{
	m_pProfiler->ResetMeans();
}
} // namespace Jettison::Render
