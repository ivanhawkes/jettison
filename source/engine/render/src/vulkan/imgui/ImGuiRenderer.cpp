#include "vulkan/imgui/ImGuiRenderer.h"

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>


namespace Jettison::Render
{
void ImGuiRenderer::Init(GLFWwindow *window, VkFormat colorFormat, uint32_t frameCount)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	m_contextCreated = true;

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	for (const auto &font : {"DroidSans.ttf", "Hack-Regular.ttf", "VeraMono.ttf"})
	{
		std::optional<std::vector<std::byte>> bytes =
		    m_pAssets ? m_pAssets->Read(std::string{"fonts/"} + font) : std::nullopt;
		if (!bytes || bytes->empty())
			continue;
		std::vector<std::byte> &data = m_fonts.emplace_back(std::move(*bytes));
		ImFontConfig config;
		config.FontDataOwnedByAtlas = false;
		io.Fonts->AddFontFromMemoryTTF(data.data(), static_cast<int>(data.size()), 18.0f, &config);
	}
	io.Fonts->AddFontDefault();
	ImGui::StyleColorsDark();

	if (!ImGui_ImplGlfw_InitForVulkan(window, true))
		throw std::runtime_error("failed to initialize ImGui GLFW backend");
	m_glfwBackendInitialized = true;

	CreateDescriptorPool();
	InitVulkanBackend(colorFormat, frameCount);
}


void ImGuiRenderer::InitVulkanBackend(VkFormat colorFormat, uint32_t frameCount)
{
	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.ApiVersion = DeviceContext::kApiVersion;
	initInfo.Instance = m_pDeviceContext->GetInstance();
	initInfo.PhysicalDevice = m_pDeviceContext->GetPhysicalDevice();
	initInfo.Device = m_pDeviceContext->GetLogicalDevice();
	initInfo.QueueFamily = m_pDeviceContext->GetGraphicsQueueIndex();
	initInfo.Queue = m_pDeviceContext->GetGraphicsQueue();
	initInfo.DescriptorPool = m_descriptorPool;
	// The backend asks for at least two; its vertex buffers are cycled one set per frame.
	initInfo.MinImageCount = std::max(frameCount, 2u);
	initInfo.ImageCount = initInfo.MinImageCount;
	initInfo.UseDynamicRendering = true;
	initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineRenderingCreateInfo &rendering = initInfo.PipelineInfoMain.PipelineRenderingCreateInfo;
	rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	rendering.colorAttachmentCount = 1;
	rendering.pColorAttachmentFormats = &colorFormat;
	initInfo.CheckVkResultFn = check_vk_result;
	if (!ImGui_ImplVulkan_Init(&initInfo))
		throw std::runtime_error("failed to initialize ImGui Vulkan backend");
	m_vulkanBackendInitialized = true;
}


void ImGuiRenderer::ShutdownVulkanBackend()
{
	if (m_vulkanBackendInitialized)
	{
		ImGui_ImplVulkan_Shutdown();
		m_vulkanBackendInitialized = false;
	}
}


void ImGuiRenderer::Destroy()
{
	ShutdownVulkanBackend();
	if (m_glfwBackendInitialized)
	{
		ImGui_ImplGlfw_Shutdown();
		m_glfwBackendInitialized = false;
	}
	if (m_contextCreated)
	{
		ImGui::DestroyContext();
		m_contextCreated = false;
	}
	m_fonts.clear();
	if (m_descriptorPool != VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(m_pDeviceContext->GetLogicalDevice(), m_descriptorPool, nullptr);
		m_descriptorPool = VK_NULL_HANDLE;
	}
}


void ImGuiRenderer::BeginFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}


void ImGuiRenderer::EndFrame()
{
	ImGui::Render();
}


void ImGuiRenderer::RenderDrawData(VkCommandBuffer commandBuffer) const
{
	ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}


void ImGuiRenderer::CreateDescriptorPool()
{
	std::array<VkDescriptorPoolSize, 11> poolSizes{{
	    {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
	    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
	    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
	    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
	}};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolInfo.maxSets = static_cast<uint32_t>(1000 * poolSizes.size());
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	if (vkCreateDescriptorPool(
	        m_pDeviceContext->GetLogicalDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
		throw std::runtime_error("failed to create ImGui descriptor pool");
}
} // namespace Jettison::Render
