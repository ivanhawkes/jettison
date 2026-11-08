#include "ImGuiPipeline.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <filesystem>


namespace Jettison::Renderer
{
void ImGuiPipeline::Init()
{
	// Setup Vulkan
	SetupVulkan();

	// Create Framebuffers
	SetupVulkanWindow();

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	(void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
	// io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
	for (const auto &font : {"DroidSans.ttf", "Hack-Regular.ttf", "VeraMono.ttf"})
	{
		const std::string path = (m_assetDirectory / "fonts" / font).string();
		if (std::filesystem::exists(path))
		{
			for (float size : {20.0f, 23.0f, 26.0f, 29.0f})
				io.Fonts->AddFontFromFileTTF(path.c_str(), size);
			if (std::string(font) != "DroidSans.ttf")
				io.Fonts->AddFontFromFileTTF(path.c_str(), 22.0f);
		}
	}
	io.Fonts->AddFontDefault();

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForVulkan(m_pDeviceContext->GetWindow()->GetGLFWWindow(), true);
	InitVulkanBackend();
}


void ImGuiPipeline::InitVulkanBackend()
{
	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.ApiVersion = VK_API_VERSION_1_0;
	init_info.Instance = m_pDeviceContext->GetInstance();
	init_info.PhysicalDevice = m_pDeviceContext->GetPhysicalDevice();
	init_info.Device = m_pDeviceContext->GetLogicalDevice();
	init_info.QueueFamily = m_pDeviceContext->GetGraphicsQueueIndex();
	init_info.Queue = g_Queue;
	init_info.PipelineCache = g_PipelineCache;
	init_info.DescriptorPool = g_DescriptorPool;
	init_info.Allocator = kPAllocator;
	init_info.MinImageCount = m_pSwapchain->GetImageCount();
	init_info.ImageCount = m_windowData.ImageCount;
	init_info.CheckVkResultFn = check_vk_result;
	init_info.PipelineInfoMain.RenderPass = m_windowData.RenderPass;
	ImGui_ImplVulkan_Init(&init_info);
}


void ImGuiPipeline::Recreate()
{
	check_vk_result(m_pDeviceContext->WaitIdle());
	ImGui_ImplVulkan_Shutdown();
	DestroyWindow();
	m_pSwapchain->Recreate();
	SetupVulkanWindow();
	InitVulkanBackend();
}


void ImGuiPipeline::Destroy()
{
}


void ImGuiPipeline::Create()
{
}


void ImGuiPipeline::CreateRenderPass()
{
	VkResult err;

	VkAttachmentDescription attachmentDescription{};
	attachmentDescription.format = m_windowData.SurfaceFormat.format;
	attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
	attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference attachmentReference{};
	attachmentReference.attachment = 0;
	attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &attachmentReference;

	VkSubpassDependency subpassDependency{};
	subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependency.dstSubpass = 0;
	subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependency.srcAccessMask = 0;
	subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &attachmentDescription;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &subpassDependency;
	err = vkCreateRenderPass(
	    m_pDeviceContext->GetLogicalDevice(), &renderPassInfo, kPAllocator, &m_windowData.RenderPass);
	check_vk_result(err);

	// We do not create a pipeline by default as this is also used by examples' main.cpp,
	// but secondary viewport in multi-viewport mode may want to create one with:
	// ImGui_ImplVulkan_CreatePipeline(m_pDeviceContext->GetLogicalDevice(), kPAllocator,
	// VK_NULL_HANDLE, m_windowData.RenderPass, VK_SAMPLE_COUNT_1_BIT, &m_windowData.Pipeline,
	// g_Subpass);
}


void ImGuiPipeline::CreateFramebuffers()
{
	VkResult err;

	VkImageView attachment[1];
	VkFramebufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.renderPass = m_windowData.RenderPass;
	info.attachmentCount = 1;
	info.pAttachments = attachment;
	info.width = m_windowData.Width;
	info.height = m_windowData.Height;
	info.layers = 1;

	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		Frame *fd = &m_windowData.Frames[i];
		attachment[0] = fd->BackbufferView;
		err = vkCreateFramebuffer(
		    m_pDeviceContext->GetLogicalDevice(), &info, kPAllocator, &fd->Framebuffer);
		check_vk_result(err);
	}
}


void ImGuiPipeline::CreateImageViews()
{
	VkResult err;

	VkImageViewCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	info.format = m_windowData.SurfaceFormat.format;
	info.components.r = VK_COMPONENT_SWIZZLE_R;
	info.components.g = VK_COMPONENT_SWIZZLE_G;
	info.components.b = VK_COMPONENT_SWIZZLE_B;
	info.components.a = VK_COMPONENT_SWIZZLE_A;

	VkImageSubresourceRange image_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	info.subresourceRange = image_range;

	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		Frame *fd = &m_windowData.Frames[i];
		info.image = fd->Backbuffer;
		err = vkCreateImageView(
		    m_pDeviceContext->GetLogicalDevice(), &info, kPAllocator, &fd->BackbufferView);
		check_vk_result(err);
	}
}


void ImGuiPipeline::CreateDescriptorPool()
{
	VkResult err;

	// Create descriptor pool. These values are much larger than we have for general use, so it can have
	// it's own private descriptor pool.
	VkDescriptorPoolSize pool_sizes[] = {
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
	    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

	VkDescriptorPoolCreateInfo pool_info{};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
	pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;
	err = vkCreateDescriptorPool(
	    m_pDeviceContext->GetLogicalDevice(), &pool_info, kPAllocator, &g_DescriptorPool);
	check_vk_result(err);
}


void ImGuiPipeline::Render()
{
	const ImVec4 kClearColour = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	VkResult err;

	ImGui::Render();
	ImDrawData *drawData = ImGui::GetDrawData();

	if (!m_pSwapchain->IsMinimised())
	{
		memcpy(&m_windowData.ClearValue.color.float32[0], &kClearColour, 4 * sizeof(float));

		VkSemaphore image_acquired_semaphore =
		    m_windowData.Semaphores[m_windowData.SemaphoreIndex].ImageAcquiredSemaphore;
		VkFence acquireFence = m_acquireFences[m_windowData.SemaphoreIndex];
		if (acquireFence != VK_NULL_HANDLE)
			check_vk_result(vkWaitForFences(
			    m_pDeviceContext->GetLogicalDevice(), 1, &acquireFence, VK_TRUE, UINT64_MAX));

		err = vkAcquireNextImageKHR(
		    m_pDeviceContext->GetLogicalDevice(), m_windowData.Swapchain, UINT64_MAX,
		    image_acquired_semaphore, VK_NULL_HANDLE, &m_windowData.FrameIndex);
		if (err == VK_ERROR_OUT_OF_DATE_KHR)
		{
			g_SwapChainRebuild = true;
			return;
		}
		if (err == VK_SUBOPTIMAL_KHR)
			m_suboptimal = true;
		else
			check_vk_result(err);
		VkSemaphore render_complete_semaphore =
		    m_windowData.Semaphores[m_windowData.FrameIndex].RenderCompleteSemaphore;

		Frame *fd = &m_windowData.Frames[m_windowData.FrameIndex];
		{
			err = vkWaitForFences(
			    m_pDeviceContext->GetLogicalDevice(), 1, &fd->Fence, VK_TRUE,
			    UINT64_MAX); // wait indefinitely instead of periodically checking
			check_vk_result(err);

			err = vkResetFences(m_pDeviceContext->GetLogicalDevice(), 1, &fd->Fence);
			check_vk_result(err);
		}
		{
			err = vkResetCommandPool(m_pDeviceContext->GetLogicalDevice(), fd->CommandPool, 0);
			check_vk_result(err);
			VkCommandBufferBeginInfo info{};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
			check_vk_result(err);
		}
		{
			VkRenderPassBeginInfo info{};
			info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			info.renderPass = m_windowData.RenderPass;
			info.framebuffer = fd->Framebuffer;
			info.renderArea.extent.width = m_windowData.Width;
			info.renderArea.extent.height = m_windowData.Height;
			info.clearValueCount = 1;
			info.pClearValues = &m_windowData.ClearValue;
			vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
		}

		// Record dear imgui primitives into command buffer
		ImGui_ImplVulkan_RenderDrawData(drawData, fd->CommandBuffer);

		// Submit command buffer
		vkCmdEndRenderPass(fd->CommandBuffer);
		{
			VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			VkSubmitInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			info.waitSemaphoreCount = 1;
			info.pWaitSemaphores = &image_acquired_semaphore;
			info.pWaitDstStageMask = &wait_stage;
			info.commandBufferCount = 1;
			info.pCommandBuffers = &fd->CommandBuffer;
			info.signalSemaphoreCount = 1;
			info.pSignalSemaphores = &render_complete_semaphore;

			err = vkEndCommandBuffer(fd->CommandBuffer);
			check_vk_result(err);
			err = vkQueueSubmit(g_Queue, 1, &info, fd->Fence);
			check_vk_result(err);
			m_acquireFences[m_windowData.SemaphoreIndex] = fd->Fence;
		}
	}
}


void ImGuiPipeline::Present()
{
	if (g_SwapChainRebuild)
		return;

	if (!m_pSwapchain->IsMinimised())
	{
		VkSemaphore render_complete_semaphore =
		    m_windowData.Semaphores[m_windowData.FrameIndex].RenderCompleteSemaphore;
		VkPresentInfoKHR info{};
		info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &render_complete_semaphore;
		info.swapchainCount = 1;
		info.pSwapchains = &m_windowData.Swapchain;
		info.pImageIndices = &m_windowData.FrameIndex;

		VkResult err = vkQueuePresentKHR(m_pDeviceContext->GetPresentQueue(), &info);
		if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
		{
			g_SwapChainRebuild = true;
			return;
		}
		check_vk_result(err);
		if (m_suboptimal)
			g_SwapChainRebuild = true;

		// Now we can use the next set of semaphores.
		m_windowData.SemaphoreIndex = (m_windowData.SemaphoreIndex + 1) % m_windowData.ImageCount;
	}
}


int ImGuiPipeline::GetMinImageCountFromPresentMode(VkPresentModeKHR present_mode)
{
	if (present_mode == VK_PRESENT_MODE_MAILBOX_KHR)
		return 3;
	if (present_mode == VK_PRESENT_MODE_FIFO_KHR || present_mode == VK_PRESENT_MODE_FIFO_RELAXED_KHR)
		return 2;
	if (present_mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
		return 1;

	IM_ASSERT(0);
	return 1;
}


void ImGuiPipeline::DestroyFrame(Frame *fd)
{
	vkDestroyFence(m_pDeviceContext->GetLogicalDevice(), fd->Fence, kPAllocator);
	vkFreeCommandBuffers(m_pDeviceContext->GetLogicalDevice(), fd->CommandPool, 1, &fd->CommandBuffer);
	vkDestroyCommandPool(m_pDeviceContext->GetLogicalDevice(), fd->CommandPool, kPAllocator);

	fd->Fence = VK_NULL_HANDLE;
	fd->CommandBuffer = VK_NULL_HANDLE;
	fd->CommandPool = VK_NULL_HANDLE;

	vkDestroyFramebuffer(m_pDeviceContext->GetLogicalDevice(), fd->Framebuffer, kPAllocator);
	fd->Framebuffer = VK_NULL_HANDLE;
	vkDestroyImageView(m_pDeviceContext->GetLogicalDevice(), fd->BackbufferView, kPAllocator);
	fd->BackbufferView = VK_NULL_HANDLE;
}


void ImGuiPipeline::DestroyFrameSemaphores(FrameSemaphores *fsd)
{
	vkDestroySemaphore(m_pDeviceContext->GetLogicalDevice(), fsd->ImageAcquiredSemaphore, kPAllocator);
	vkDestroySemaphore(m_pDeviceContext->GetLogicalDevice(), fsd->RenderCompleteSemaphore, kPAllocator);

	fsd->ImageAcquiredSemaphore = fsd->RenderCompleteSemaphore = VK_NULL_HANDLE;
}


void ImGuiPipeline::DestroyWindow()
{
	// FIXME: We could wait on the Queue if we had the queue in m_windowData. (otherwise VulkanH
	// functions can't use globals)
	vkDeviceWaitIdle(m_pDeviceContext->GetLogicalDevice());
	// vkQueueWaitIdle(g_Queue);

	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		DestroyFrame(&m_windowData.Frames[i]);
		DestroyFrameSemaphores(&m_windowData.Semaphores[i]);
	}

	m_windowData.Frames.clear();
	m_windowData.Semaphores.clear();

	vkDestroyPipeline(m_pDeviceContext->GetLogicalDevice(), m_windowData.Pipeline, kPAllocator);
	vkDestroyRenderPass(m_pDeviceContext->GetLogicalDevice(), m_windowData.RenderPass, kPAllocator);

	// Better clear that window data.
	m_windowData = WindowData{};
}


void ImGuiPipeline::CreateWindowSwapChain()
{
	VkResult err;

	err = vkDeviceWaitIdle(m_pDeviceContext->GetLogicalDevice());
	check_vk_result(err);

	// TODO: Out of date. Work out how I want to handle this in line with how the other pipeline is
	// doing it. We don't use DestroyWindow() because we want to preserve the old swapchain to create
	// the new one. Destroy old Framebuffer
	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		DestroyFrame(&m_windowData.Frames[i]);
		DestroyFrameSemaphores(&m_windowData.Semaphores[i]);
	}

	m_windowData.Frames.clear();
	m_windowData.Semaphores.clear();
	m_windowData.ImageCount = 0;

	if (m_windowData.RenderPass)
		vkDestroyRenderPass(m_pDeviceContext->GetLogicalDevice(), m_windowData.RenderPass, kPAllocator);

	if (m_windowData.Pipeline)
		vkDestroyPipeline(m_pDeviceContext->GetLogicalDevice(), m_windowData.Pipeline, kPAllocator);

	// Create Swapchain
	{
		m_windowData.Swapchain = m_pSwapchain->GetVkSwapchainHandle();
		m_windowData.ImageCount = m_pSwapchain->GetImageCount();
		m_windowData.Height = m_pSwapchain->GetExtents().height;
		m_windowData.Width = m_pSwapchain->GetExtents().width;

		err = vkGetSwapchainImagesKHR(
		    m_pDeviceContext->GetLogicalDevice(), m_windowData.Swapchain, &m_windowData.ImageCount,
		    nullptr);
		check_vk_result(err);

		std::vector<VkImage> backbuffers(m_windowData.ImageCount);
		err = vkGetSwapchainImagesKHR(
		    m_pDeviceContext->GetLogicalDevice(), m_windowData.Swapchain, &m_windowData.ImageCount,
		    backbuffers.data());
		check_vk_result(err);

		// auto imageCount = m_pSwapchain->GetImageCount();
		// std::vector<VkImage> swapchainImages {};
		// swapchainImages.resize(imageCount);

		// vkGetSwapchainImagesKHR(m_pDeviceContext->GetLogicalDevice(),
		// m_pSwapchain->GetVkSwapchainHandle(), m_pSwapchain->GetImageCountAddress(), nullptr);
		// vkGetSwapchainImagesKHR(m_pDeviceContext->GetLogicalDevice(),
		// m_pSwapchain->GetVkSwapchainHandle(), m_pSwapchain->GetImageCountAddress(),
		// swapchainImages.data());

		m_windowData.Frames.resize(m_windowData.ImageCount);
		m_windowData.Semaphores.resize(m_windowData.ImageCount);
		for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
			m_windowData.Frames[i].Backbuffer = backbuffers[i];
	}

	CreateRenderPass();

	CreateImageViews();

	CreateFramebuffers();
}


void ImGuiPipeline::CreateWindowCommandBuffers()
{
	// Create Command Buffers
	VkResult err;
	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		Frame *fd = &m_windowData.Frames[i];
		FrameSemaphores *fsd = &m_windowData.Semaphores[i];

		{
			VkCommandPoolCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			info.queueFamilyIndex = m_pDeviceContext->GetGraphicsQueueIndex();
			err = vkCreateCommandPool(
			    m_pDeviceContext->GetLogicalDevice(), &info, kPAllocator, &fd->CommandPool);
			check_vk_result(err);
		}
		{
			VkCommandBufferAllocateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.commandPool = fd->CommandPool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = 1;
			err = vkAllocateCommandBuffers(
			    m_pDeviceContext->GetLogicalDevice(), &info, &fd->CommandBuffer);
			check_vk_result(err);
		}
		{
			VkFenceCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			err = vkCreateFence(m_pDeviceContext->GetLogicalDevice(), &info, kPAllocator, &fd->Fence);
			check_vk_result(err);
		}
		{
			VkSemaphoreCreateInfo info{};
			info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			err = vkCreateSemaphore(
			    m_pDeviceContext->GetLogicalDevice(), &info, kPAllocator, &fsd->ImageAcquiredSemaphore);
			check_vk_result(err);
			err = vkCreateSemaphore(
			    m_pDeviceContext->GetLogicalDevice(), &info, kPAllocator,
			    &fsd->RenderCompleteSemaphore);
			check_vk_result(err);
		}
	}
}


// Create or resize window
void ImGuiPipeline::CreateOrResizeWindow(uint32_t queue_family)
{
	CreateWindowSwapChain();
	CreateWindowCommandBuffers();

	m_windowData.FrameIndex = 0;
	m_windowData.SemaphoreIndex = 0;
	m_acquireFences.assign(m_windowData.ImageCount, VK_NULL_HANDLE);
	m_suboptimal = false;
	g_SwapChainRebuild = false;
}


void ImGuiPipeline::SetupVulkan()
{
	// We require a logical device with 1 queue.
	vkGetDeviceQueue(
	    m_pDeviceContext->GetLogicalDevice(), m_pDeviceContext->GetGraphicsQueueIndex(), 0, &g_Queue);

	CreateDescriptorPool();
}


void ImGuiPipeline::SetupVulkanWindow()
{
	m_windowData.Surface = m_pDeviceContext->GetSurface();

	m_windowData.SurfaceFormat.format = m_pSwapchain->GetImageFormat();

	// Select present mode.
#ifdef IMGUI_UNLIMITED_FRAME_RATE
	std::vector<VkPresentModeKHR> presentModeCandiates{
	    VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
#else
	std::vector<VkPresentModeKHR> presentModeCandiates{VK_PRESENT_MODE_FIFO_KHR};
#endif
	m_windowData.PresentMode = m_pDeviceContext->FindSupportedPresentMode(presentModeCandiates);

	// Create SwapChain, RenderPass, Framebuffer, etc.
	CreateOrResizeWindow(m_pDeviceContext->GetGraphicsQueueIndex());
}


void ImGuiPipeline::CleanupVulkan()
{
	vkDestroyDescriptorPool(m_pDeviceContext->GetLogicalDevice(), g_DescriptorPool, kPAllocator);
}


void ImGuiPipeline::CleanupVulkanWindow()
{
	DestroyWindow();
}
} // namespace Jettison::Renderer
