#include <jettison/render/vulkan/PipelineCache.h>

#include <array>
#include <stdexcept>
#include <vector>

#include <jettison/render/vulkan/DeviceContext.h>
#include <jettison/render/vulkan/Model.h>
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


void PipelineCache::Destroy()
{
	for (const auto &[key, pipeline] : m_pipelines)
		vkDestroyPipeline(m_pDeviceContext->GetLogicalDevice(), pipeline, nullptr);
	m_pipelines.clear();
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

	const VkShaderModule vertexModule = LoadModule("shaders/" + key.program + ".vert.spv");
	VkShaderModule fragmentModule = VK_NULL_HANDLE;
	try
	{
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
	const VkVertexInputBindingDescription meshBinding = Vertex::getBindingDescription();
	const auto meshAttributes = Vertex::getAttributeDescriptions();
	if (state.vertices == VertexLayout::Mesh)
	{
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &meshBinding;
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(meshAttributes.size());
		vertexInput.pVertexAttributeDescriptions = meshAttributes.data();
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

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = state.samples;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = state.depthTest ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = state.depthWrite ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = state.depthCompare;

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
	VkPipelineColorBlendStateCreateInfo blending{};
	blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	blending.attachmentCount = 1;
	blending.pAttachments = &blendAttachment;

	const std::array<VkDynamicState, 2> dynamicStates{
	    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	VkPipelineRenderingCreateInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &state.colorFormat;
	rendering.depthAttachmentFormat = state.depthFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &rendering;
	pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
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
} // namespace Jettison::Render
