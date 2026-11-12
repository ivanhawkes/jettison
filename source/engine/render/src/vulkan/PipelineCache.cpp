#include "vulkan/PipelineCache.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "vulkan/DeviceContext.h"
#include "vulkan/Model.h"
#include <jettison/render/DebugDraw.h>
#include <jettison/render/Overlay.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::Render
{
PipelineCache::PipelineCache(
    std::shared_ptr<DeviceContext> pDeviceContext, const Vfs::FileSystem &assets)
    : m_pDeviceContext{std::move(pDeviceContext)}, m_assets{assets}
{
}


VkPipeline PipelineCache::Get(const PipelineKey &key)
{
	if (const auto found = m_pipelines.find(key); found != m_pipelines.end())
		return found->second;
	const VkPipeline pipeline = Create(key);
	m_pipelines.emplace(key, pipeline);
	return pipeline;
}


VkPipeline PipelineCache::GetCompute(const std::string &program, VkPipelineLayout layout)
{
	const auto key = std::make_pair(program, layout);
	if (const auto found = m_computePipelines.find(key); found != m_computePipelines.end())
		return found->second;
	const VkPipeline pipeline = CreateCompute(program, layout);
	m_computePipelines.emplace(key, pipeline);
	return pipeline;
}


void PipelineCache::Destroy()
{
	for (const auto &[key, pipeline] : m_pipelines)
		vkDestroyPipeline(m_pDeviceContext->GetLogicalDevice(), pipeline, nullptr);
	for (const auto &[key, pipeline] : m_computePipelines)
		vkDestroyPipeline(m_pDeviceContext->GetLogicalDevice(), pipeline, nullptr);
	m_pipelines.clear();
	m_computePipelines.clear();
}


VkShaderModule PipelineCache::LoadModule(const std::string &virtualPath)
{
	// The words "failed to open file" are what the render test's start-up check reads for: a run
	// with no shaders has to stop here, before a frame, and say which file it wanted.
	const auto code = m_assets.Read(virtualPath);
	if (!code)
		throw std::runtime_error("failed to open file: " + virtualPath + " (no asset mount has it)");
	const VkShaderModule module = m_pDeviceContext->CreateShaderModule(*code, virtualPath);
	m_pDeviceContext->SetDebugName(module, virtualPath);
	return module;
}


VkPipeline PipelineCache::Create(const PipelineKey &key)
{
	const RenderState &state = key.state;
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();

	// A depth-only pass writes no colour, and a program for it has no fragment stage to load.
	const bool depthOnly = state.colorFormat == VK_FORMAT_UNDEFINED;
	const VkShaderModule vertexModule = LoadModule("shaders/" + key.program + ".vert.spv");
	VkShaderModule fragmentModule = VK_NULL_HANDLE;
	try
	{
		if (!depthOnly)
			fragmentModule = LoadModule("shaders/" + key.program + ".frag.spv");
	}
	catch (...)
	{
		vkDestroyShaderModule(device, vertexModule, nullptr);
		throw;
	}

	std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertexModule;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragmentModule;
	stages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertexInput{};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	const VkVertexInputBindingDescription meshBinding = VertexBindingDescription();
	const auto meshAttributes = VertexAttributeDescriptions();
	const VkVertexInputBindingDescription lineBinding{
	    0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX};
	const std::array<VkVertexInputAttributeDescription, 2> lineAttributes{{
	    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, position)},
	    {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LineVertex, color)},
	}};
	const VkVertexInputBindingDescription overlayBinding{
	    0, sizeof(OverlayVertex), VK_VERTEX_INPUT_RATE_VERTEX};
	const std::array<VkVertexInputAttributeDescription, 3> overlayAttributes{{
	    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, position)},
	    {1, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(OverlayVertex, color)},
	    {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(OverlayVertex, uv)},
	}};
	if (state.vertices == VertexLayout::Mesh)
	{
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &meshBinding;
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(meshAttributes.size());
		vertexInput.pVertexAttributeDescriptions = meshAttributes.data();
	}
	else if (state.vertices == VertexLayout::Line)
	{
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &lineBinding;
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(lineAttributes.size());
		vertexInput.pVertexAttributeDescriptions = lineAttributes.data();
	}
	else if (state.vertices == VertexLayout::Overlay)
	{
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &overlayBinding;
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(overlayAttributes.size());
		vertexInput.pVertexAttributeDescriptions = overlayAttributes.data();
	}

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = state.topology;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = state.cullMode;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthClampEnable = state.depthClamp ? VK_TRUE : VK_FALSE;
	if (state.depthBiasConstant != 0.0f || state.depthBiasSlope != 0.0f)
	{
		rasterizer.depthBiasEnable = VK_TRUE;
		rasterizer.depthBiasConstantFactor = state.depthBiasConstant;
		rasterizer.depthBiasSlopeFactor = state.depthBiasSlope;
	}

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = state.samples;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = state.depthTest ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = state.depthWrite ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = state.depthCompare;
	if (state.stencilTest)
	{
		depthStencil.stencilTestEnable = VK_TRUE;
		const VkStencilOpState face{
		    VK_STENCIL_OP_KEEP,
		    state.stencilPass,
		    VK_STENCIL_OP_KEEP,
		    state.stencilCompare,
		    state.stencilCompareMask,
		    state.stencilWriteMask,
		    0};
		depthStencil.front = face;
		depthStencil.back = face;
	}

	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	if (state.blend == BlendMode::Alpha)
	{
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	else if (state.blend == BlendMode::Premultiplied)
	{
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	else if (state.blend == BlendMode::Additive)
	{
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	else if (state.blend == BlendMode::Transmissive)
	{
		blendAttachment.blendEnable = VK_TRUE;
		blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC1_COLOR;
		blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC1_ALPHA;
		blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	}
	// The second target, when there is one, takes what is written whole.
	VkPipelineColorBlendAttachmentState secondAttachment{};
	secondAttachment.colorWriteMask = blendAttachment.colorWriteMask;
	const std::array<VkPipelineColorBlendAttachmentState, 2> blendAttachments{
	    blendAttachment, secondAttachment};
	const std::array<VkFormat, 2> colorFormats{state.colorFormat, state.secondColorFormat};
	const uint32_t colorCount =
	    depthOnly ? 0u : (state.secondColorFormat != VK_FORMAT_UNDEFINED ? 2u : 1u);
	VkPipelineColorBlendStateCreateInfo blending{};
	blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blending.attachmentCount = colorCount;
	blending.pAttachments = blendAttachments.data();

	// The stencil reference is the draw's, so a batch's marks need no pipeline of their own.
	const std::array<VkDynamicState, 3> dynamicStates{
	    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_STENCIL_REFERENCE};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = state.stencilTest ? 3u : 2u;
	dynamicState.pDynamicStates = dynamicStates.data();

	VkPipelineRenderingCreateInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = colorCount;
	rendering.pColorAttachmentFormats = colorFormats.data();
	rendering.depthAttachmentFormat = state.depthFormat;
	rendering.stencilAttachmentFormat = state.stencilFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &rendering;
	pipelineInfo.stageCount = depthOnly ? 1 : static_cast<uint32_t>(stages.size());
	pipelineInfo.pStages = stages.data();
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &blending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = key.layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	const VkResult result =
	    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
	vkDestroyShaderModule(device, fragmentModule, nullptr);
	vkDestroyShaderModule(device, vertexModule, nullptr);
	if (result != VK_SUCCESS)
		throw std::runtime_error("failed to create the pipeline for program '" + key.program + "'");
	m_pDeviceContext->SetDebugName(pipeline, "pipeline-" + key.program);
	return pipeline;
}


VkPipeline PipelineCache::CreateCompute(const std::string &program, VkPipelineLayout layout)
{
	const VkDevice device = m_pDeviceContext->GetLogicalDevice();
	const VkShaderModule module = LoadModule("shaders/" + program + ".comp.spv");

	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipelineInfo.stage.module = module;
	pipelineInfo.stage.pName = "main";
	pipelineInfo.layout = layout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	const VkResult result =
	    vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
	vkDestroyShaderModule(device, module, nullptr);
	if (result != VK_SUCCESS)
		throw std::runtime_error("failed to create the compute pipeline for program '" + program + "'");
	m_pDeviceContext->SetDebugName(pipeline, "pipeline-" + program);
	return pipeline;
}
} // namespace Jettison::Render
