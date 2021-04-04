#include "ImGuiPipeline.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <../glfw/include/GLFW/glfw3.h>


namespace Jettison::Renderer
{
void ImGuiPipeline::Init()
{
	// Setup Vulkan
	SetupVulkan(m_pDeviceContext);

	// Create Framebuffers
	SetupVulkanWindow(m_pDeviceContext, m_pSwapchain);

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer bindings
	ImGui_ImplGlfw_InitForVulkan(m_pDeviceContext->GetWindow()->GetGLFWWindow(), true);
	ImGui_ImplVulkan_InitInfo init_info {};
	init_info.Instance = m_pDeviceContext->GetInstance();
	init_info.PhysicalDevice = m_pDeviceContext->GetPhysicalDevice();
	init_info.Device = m_pDeviceContext->GetLogicalDevice();
	init_info.QueueFamily = m_pDeviceContext->GetGraphicsQueueIndex();
	init_info.Queue = g_Queue;
	init_info.PipelineCache = g_PipelineCache;
	init_info.DescriptorPool = g_DescriptorPool;
	init_info.Allocator = g_Allocator;
	init_info.MinImageCount = m_pSwapchain->GetImageCount();
	init_info.ImageCount = m_windowData.ImageCount;
	init_info.CheckVkResultFn = check_vk_result;
	ImGui_ImplVulkan_Init(&init_info, m_windowData.RenderPass);

	// Load the font list into the font texture.
	ImGuiInitFontTexture(m_pDeviceContext);
}


void ImGuiPipeline::Recreate()
{
}


void ImGuiPipeline::Destroy()
{
}


void ImGuiPipeline::Create()
{
}


void ImGuiPipeline::ImGuiInitFontTexture(std::shared_ptr<Jettison::Renderer::DeviceContext> m_pDeviceContext)
{
	ImGuiIO& io = ImGui::GetIO();

	// Load several different sizes of the VeraMono font supplied by CryTek.
	// NOTE: The first font loaded seems to be the default one used for the UI.
	const std::string droidSansPath {"assets\\fonts\\DroidSans.ttf"};
	io.Fonts->AddFontFromFileTTF(droidSansPath.c_str(), 20.0f);
	io.Fonts->AddFontFromFileTTF(droidSansPath.c_str(), 23.0f);
	io.Fonts->AddFontFromFileTTF(droidSansPath.c_str(), 26.0f);
	io.Fonts->AddFontFromFileTTF(droidSansPath.c_str(), 29.0f);

	const std::string hackRegularPath {"assets\\fonts\\Hack-Regular.ttf"};
	io.Fonts->AddFontFromFileTTF(hackRegularPath.c_str(), 20.0f);
	io.Fonts->AddFontFromFileTTF(hackRegularPath.c_str(), 22.0f);
	io.Fonts->AddFontFromFileTTF(hackRegularPath.c_str(), 23.0f);
	io.Fonts->AddFontFromFileTTF(hackRegularPath.c_str(), 26.0f);
	io.Fonts->AddFontFromFileTTF(hackRegularPath.c_str(), 29.0f);

	const std::string veraMonoPath {"assets\\fonts\\VeraMono.ttf"};
	io.Fonts->AddFontFromFileTTF(veraMonoPath.c_str(), 20.0f);
	io.Fonts->AddFontFromFileTTF(veraMonoPath.c_str(), 22.0f);
	io.Fonts->AddFontFromFileTTF(veraMonoPath.c_str(), 23.0f);
	io.Fonts->AddFontFromFileTTF(veraMonoPath.c_str(), 26.0f);
	io.Fonts->AddFontFromFileTTF(veraMonoPath.c_str(), 29.0f);

	// May as well have the default font.
	io.Fonts->AddFontDefault();

	// Use the single time command buffers for loading the fonts.
	VkCommandBuffer command_buffer = m_pDeviceContext->BeginSingleTimeCommands();
	ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
	m_pDeviceContext->EndSingleTimeCommands(command_buffer);
}


void ImGuiPipeline::FrameRender()
{
	const ImVec4 kClearColour = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

	VkResult err;

	ImGui::Render();
	ImDrawData* drawData = ImGui::GetDrawData();

	if (!m_pSwapchain->IsMinimised())
	{
		memcpy(&m_windowData.ClearValue.color.float32[0], &kClearColour, 4 * sizeof(float));

		VkSemaphore image_acquired_semaphore = m_windowData.FrameSemaphores[m_windowData.SemaphoreIndex].ImageAcquiredSemaphore;
		VkSemaphore render_complete_semaphore = m_windowData.FrameSemaphores[m_windowData.SemaphoreIndex].RenderCompleteSemaphore;

		err = vkAcquireNextImageKHR(m_pDeviceContext->GetLogicalDevice(), m_windowData.Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &m_windowData.FrameIndex);
		if (err == VK_ERROR_OUT_OF_DATE_KHR)
		{
			g_SwapChainRebuild = true;
			return;
		}
		check_vk_result(err);

		ImGui_ImplVulkanH_Frame* fd = &m_windowData.Frames[m_windowData.FrameIndex];
		{
			err = vkWaitForFences(m_pDeviceContext->GetLogicalDevice(), 1, &fd->Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
			check_vk_result(err);

			err = vkResetFences(m_pDeviceContext->GetLogicalDevice(), 1, &fd->Fence);
			check_vk_result(err);
		}
		{
			err = vkResetCommandPool(m_pDeviceContext->GetLogicalDevice(), fd->CommandPool, 0);
			check_vk_result(err);
			VkCommandBufferBeginInfo info {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
			check_vk_result(err);
		}
		{
			VkRenderPassBeginInfo info {};
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
			VkSubmitInfo info {};
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
		}
	}
}


void ImGuiPipeline::FramePresent()
{
	if (g_SwapChainRebuild)
		return;

	if (!m_pSwapchain->IsMinimised())
	{
		VkSemaphore render_complete_semaphore = m_windowData.FrameSemaphores[m_windowData.SemaphoreIndex].RenderCompleteSemaphore;
		VkPresentInfoKHR info {};
		info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores = &render_complete_semaphore;
		info.swapchainCount = 1;
		info.pSwapchains = &m_windowData.Swapchain;
		info.pImageIndices = &m_windowData.FrameIndex;
		
		VkResult err = vkQueuePresentKHR(g_Queue, &info);
		if (err == VK_ERROR_OUT_OF_DATE_KHR)
		{
			g_SwapChainRebuild = true;
			return;
		}
		check_vk_result(err);
		m_windowData.SemaphoreIndex = (m_windowData.SemaphoreIndex + 1) % m_windowData.ImageCount; // Now we can use the next set of semaphores
	}
}


int ImGuiPipeline::ImGuiGetMinImageCountFromPresentMode(VkPresentModeKHR present_mode)
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


void ImGuiPipeline::ImGuiDestroyFrame(VkDevice device, ImGui_ImplVulkanH_Frame* fd, const VkAllocationCallbacks* allocator)
{
	vkDestroyFence(device, fd->Fence, allocator);
	vkFreeCommandBuffers(device, fd->CommandPool, 1, &fd->CommandBuffer);
	vkDestroyCommandPool(device, fd->CommandPool, allocator);
	fd->Fence = VK_NULL_HANDLE;
	fd->CommandBuffer = VK_NULL_HANDLE;
	fd->CommandPool = VK_NULL_HANDLE;

	vkDestroyImageView(device, fd->BackbufferView, allocator);
	fd->BackbufferView = VK_NULL_HANDLE;
	vkDestroyFramebuffer(device, fd->Framebuffer, allocator);
	fd->Framebuffer = VK_NULL_HANDLE;
}


void ImGuiPipeline::ImGuiDestroyFrameSemaphores(VkDevice device, ImGui_ImplVulkanH_FrameSemaphores* fsd, const VkAllocationCallbacks* allocator)
{
	vkDestroySemaphore(device, fsd->ImageAcquiredSemaphore, allocator);
	vkDestroySemaphore(device, fsd->RenderCompleteSemaphore, allocator);
	fsd->ImageAcquiredSemaphore = fsd->RenderCompleteSemaphore = VK_NULL_HANDLE;
}


void ImGuiPipeline::ImGuiDestroyWindow(VkInstance instance, VkDevice device, const VkAllocationCallbacks* allocator)
{
	// FIXME: We could wait on the Queue if we had the queue in m_windowData. (otherwise VulkanH functions can't use globals)
	vkDeviceWaitIdle(device);
	//vkQueueWaitIdle(g_Queue);

	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		ImGuiDestroyFrame(device, &m_windowData.Frames[i], allocator);
		ImGuiDestroyFrameSemaphores(device, &m_windowData.FrameSemaphores[i], allocator);
	}

	IM_FREE(m_windowData.Frames);
	IM_FREE(m_windowData.FrameSemaphores);

	m_windowData.Frames = nullptr;
	m_windowData.FrameSemaphores = nullptr;

	vkDestroyPipeline(device, m_windowData.Pipeline, allocator);
	vkDestroyRenderPass(device, m_windowData.RenderPass, allocator);

	// Better clear that window data.
	m_windowData = ImGui_ImplVulkanH_Window {};
}


void ImGuiPipeline::ImGuiDestroyFrameRenderBuffers(VkDevice device, ImGui_ImplVulkanH_FrameRenderBuffers* buffers, const VkAllocationCallbacks* allocator)
{
	if (buffers->VertexBuffer) { vkDestroyBuffer(device, buffers->VertexBuffer, allocator); buffers->VertexBuffer = VK_NULL_HANDLE; }
	if (buffers->VertexBufferMemory) { vkFreeMemory(device, buffers->VertexBufferMemory, allocator); buffers->VertexBufferMemory = VK_NULL_HANDLE; }
	if (buffers->IndexBuffer) { vkDestroyBuffer(device, buffers->IndexBuffer, allocator); buffers->IndexBuffer = VK_NULL_HANDLE; }
	if (buffers->IndexBufferMemory) { vkFreeMemory(device, buffers->IndexBufferMemory, allocator); buffers->IndexBufferMemory = VK_NULL_HANDLE; }
	buffers->VertexBufferSize = 0;
	buffers->IndexBufferSize = 0;
}


void ImGuiPipeline::ImGuiDestroyWindowRenderBuffers(VkDevice device, ImGui_ImplVulkanH_WindowRenderBuffers* buffers, const VkAllocationCallbacks* allocator)
{
	for (uint32_t n = 0; n < buffers->Count; n++)
		ImGuiDestroyFrameRenderBuffers(device, &buffers->FrameRenderBuffers[n], allocator);
	IM_FREE(buffers->FrameRenderBuffers);
	buffers->FrameRenderBuffers = nullptr;
	buffers->Index = 0;
	buffers->Count = 0;
}


void ImGuiPipeline::ImGuiCreateWindowSwapChain(std::shared_ptr<Jettison::Renderer::DeviceContext> m_pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain,
	const VkAllocationCallbacks* allocator)
{
	VkResult err;

	//VkSwapchainKHR old_swapchain = m_windowData.Swapchain;
	//m_windowData.Swapchain = nullptr;

	err = vkDeviceWaitIdle(m_pDeviceContext->GetLogicalDevice());
	check_vk_result(err);


	// TODO: *** Figure out why the screen doesn't update after a resize - is it the extents again?


	// We don't use ImGuiDestroyWindow() because we want to preserve the old swapchain to create the new one.
	// Destroy old Framebuffer
	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		ImGuiDestroyFrame(m_pDeviceContext->GetLogicalDevice(), &m_windowData.Frames[i], allocator);
		ImGuiDestroyFrameSemaphores(m_pDeviceContext->GetLogicalDevice(), &m_windowData.FrameSemaphores[i], allocator);
	}
	IM_FREE(m_windowData.Frames);
	IM_FREE(m_windowData.FrameSemaphores);
	m_windowData.Frames = nullptr;
	m_windowData.FrameSemaphores = nullptr;
	m_windowData.ImageCount = 0;
	if (m_windowData.RenderPass)
		vkDestroyRenderPass(m_pDeviceContext->GetLogicalDevice(), m_windowData.RenderPass, allocator);
	if (m_windowData.Pipeline)
		vkDestroyPipeline(m_pDeviceContext->GetLogicalDevice(), m_windowData.Pipeline, allocator);

	// Create Swapchain
	{
		m_windowData.Swapchain = m_pSwapchain->GetVkSwapchainHandle();
		m_windowData.ImageCount = m_pSwapchain->GetImageCount();
		m_windowData.Height = m_pSwapchain->GetExtents().height;
		m_windowData.Width = m_pSwapchain->GetExtents().width;

		err = vkGetSwapchainImagesKHR(m_pDeviceContext->GetLogicalDevice(), m_windowData.Swapchain, &m_windowData.ImageCount, nullptr);
		check_vk_result(err);

		VkImage backbuffers[16] {};
		IM_ASSERT(m_windowData.ImageCount >= m_pSwapchain->GetImageCount());
		IM_ASSERT(m_windowData.ImageCount < IM_ARRAYSIZE(backbuffers));
		err = vkGetSwapchainImagesKHR(m_pDeviceContext->GetLogicalDevice(), m_windowData.Swapchain, &m_windowData.ImageCount, backbuffers);
		check_vk_result(err);

		//auto imageCount = m_pSwapchain->GetImageCount();
		//std::vector<VkImage> swapchainImages {};
		//swapchainImages.resize(imageCount);

		//vkGetSwapchainImagesKHR(m_pDeviceContext->GetLogicalDevice(), m_pSwapchain->GetVkSwapchainHandle(), m_pSwapchain->GetImageCountAddress(), nullptr);
		//vkGetSwapchainImagesKHR(m_pDeviceContext->GetLogicalDevice(), m_pSwapchain->GetVkSwapchainHandle(), m_pSwapchain->GetImageCountAddress(), swapchainImages.data());

		IM_ASSERT(m_windowData.Frames == nullptr);
		m_windowData.Frames = (ImGui_ImplVulkanH_Frame*)IM_ALLOC(sizeof(ImGui_ImplVulkanH_Frame) * m_windowData.ImageCount);
		m_windowData.FrameSemaphores = (ImGui_ImplVulkanH_FrameSemaphores*)IM_ALLOC(sizeof(ImGui_ImplVulkanH_FrameSemaphores) * m_windowData.ImageCount);
		memset(m_windowData.Frames, 0, sizeof(m_windowData.Frames[0]) * m_windowData.ImageCount);
		memset(m_windowData.FrameSemaphores, 0, sizeof(m_windowData.FrameSemaphores[0]) * m_windowData.ImageCount);
		for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
			m_windowData.Frames[i].Backbuffer = backbuffers[i];
	}

	// Create the Render Pass
	{
		VkAttachmentDescription attachmentDescription {};
		attachmentDescription.format = m_windowData.SurfaceFormat.format;
		attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription.loadOp = m_windowData.ClearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference attachmentReference {};
		attachmentReference.attachment = 0;
		attachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpassDescription {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &attachmentReference;

		VkSubpassDependency subpassDependency {};
		subpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		subpassDependency.dstSubpass = 0;
		subpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		subpassDependency.srcAccessMask = 0;
		subpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo renderPassInfo {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &attachmentDescription;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &subpassDependency;
		err = vkCreateRenderPass(m_pDeviceContext->GetLogicalDevice(), &renderPassInfo, allocator, &m_windowData.RenderPass);
		check_vk_result(err);

		// We do not create a pipeline by default as this is also used by examples' main.cpp,
		// but secondary viewport in multi-viewport mode may want to create one with:
		//ImGui_ImplVulkan_CreatePipeline(m_pDeviceContext->GetLogicalDevice(), allocator, VK_NULL_HANDLE, m_windowData.RenderPass, VK_SAMPLE_COUNT_1_BIT, &m_windowData.Pipeline, g_Subpass);
	}

	// Create The Image Views
	{
		VkImageViewCreateInfo info {};
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
			ImGui_ImplVulkanH_Frame* fd = &m_windowData.Frames[i];
			info.image = fd->Backbuffer;
			err = vkCreateImageView(m_pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->BackbufferView);
			check_vk_result(err);
		}
	}

	// Create Framebuffer
	{
		VkImageView attachment[1];
		VkFramebufferCreateInfo info {};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.renderPass = m_windowData.RenderPass;
		info.attachmentCount = 1;
		info.pAttachments = attachment;
		info.width = m_windowData.Width;
		info.height = m_windowData.Height;
		info.layers = 1;
		for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
		{
			ImGui_ImplVulkanH_Frame* fd = &m_windowData.Frames[i];
			attachment[0] = fd->BackbufferView;
			err = vkCreateFramebuffer(m_pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->Framebuffer);
			check_vk_result(err);
		}
	}
}


void ImGuiPipeline::ImGuiCreateWindowCommandBuffers(std::shared_ptr<Jettison::Renderer::DeviceContext> m_pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain,
	const VkAllocationCallbacks* allocator)
{
	// Create Command Buffers
	VkResult err;
	for (uint32_t i = 0; i < m_windowData.ImageCount; i++)
	{
		ImGui_ImplVulkanH_Frame* fd = &m_windowData.Frames[i];
		ImGui_ImplVulkanH_FrameSemaphores* fsd = &m_windowData.FrameSemaphores[i];

		{
			VkCommandPoolCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			info.queueFamilyIndex = m_pDeviceContext->GetGraphicsQueueIndex();
			err = vkCreateCommandPool(m_pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->CommandPool);
			check_vk_result(err);
		}
		{
			VkCommandBufferAllocateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.commandPool = fd->CommandPool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = 1;
			err = vkAllocateCommandBuffers(m_pDeviceContext->GetLogicalDevice(), &info, &fd->CommandBuffer);
			check_vk_result(err);
		}
		{
			VkFenceCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			err = vkCreateFence(m_pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->Fence);
			check_vk_result(err);
		}
		{
			VkSemaphoreCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			err = vkCreateSemaphore(m_pDeviceContext->GetLogicalDevice(), &info, allocator, &fsd->ImageAcquiredSemaphore);
			check_vk_result(err);
			err = vkCreateSemaphore(m_pDeviceContext->GetLogicalDevice(), &info, allocator, &fsd->RenderCompleteSemaphore);
			check_vk_result(err);
		}
	}
}


// Create or resize window
void ImGuiPipeline::ImGuiCreateOrResizeWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> m_pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain,
	uint32_t queue_family, const VkAllocationCallbacks* allocator)
{
	ImGuiCreateWindowSwapChain(m_pDeviceContext, m_pSwapchain, allocator);
	ImGuiCreateWindowCommandBuffers(m_pDeviceContext, m_pSwapchain, allocator);

	m_windowData.FrameIndex = 0;
	g_SwapChainRebuild = false;
}


void ImGuiPipeline::SetupVulkan(std::shared_ptr<Jettison::Renderer::DeviceContext> m_pDeviceContext)
{
	VkResult err;

	g_QueueFamily = m_pDeviceContext->GetGraphicsQueueIndex();

	// We require a logical device with 1 queue.
	vkGetDeviceQueue(m_pDeviceContext->GetLogicalDevice(), m_pDeviceContext->GetGraphicsQueueIndex(), 0, &g_Queue);

	// Create descriptor pool. These values are much larger than we have for general use, so it can have it's own private descriptor pool.
	VkDescriptorPoolSize pool_sizes[] =
	{
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
		{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}
	};

	VkDescriptorPoolCreateInfo pool_info {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
	pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;
	err = vkCreateDescriptorPool(m_pDeviceContext->GetLogicalDevice(), &pool_info, g_Allocator, &g_DescriptorPool);
	check_vk_result(err);
}


void ImGuiPipeline::SetupVulkanWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> m_pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain)
{
	m_windowData.Surface = m_pDeviceContext->GetSurface();

	// Check for WSI support
	VkBool32 isSupported;
	vkGetPhysicalDeviceSurfaceSupportKHR(m_pDeviceContext->GetPhysicalDevice(), m_pDeviceContext->GetGraphicsQueueIndex(), m_windowData.Surface, &isSupported);
	if (isSupported != VK_TRUE)
	{
		throw std::runtime_error("No WSI support on physical device.");
	}

	// Select surface format.
	const std::vector<VkFormat> requestSurfaceImageFormat {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
	const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	m_windowData.SurfaceFormat = m_pDeviceContext->FindSupportedSurfaceFormat(requestSurfaceImageFormat, requestSurfaceColorSpace);

	// Select present mode.
#ifdef IMGUI_UNLIMITED_FRAME_RATE
	std::vector<VkPresentModeKHR> presentModeCandiates {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
#else
	std::vector<VkPresentModeKHR> presentModeCandiates {VK_PRESENT_MODE_FIFO_KHR};
#endif
	m_windowData.PresentMode = m_pDeviceContext->FindSupportedPresentMode(presentModeCandiates);

	// Create SwapChain, RenderPass, Framebuffer, etc.
	ImGuiCreateOrResizeWindow(m_pDeviceContext, m_pSwapchain,
		m_pDeviceContext->GetGraphicsQueueIndex(),
		g_Allocator);
}


void ImGuiPipeline::CleanupVulkan()
{
	vkDestroyDescriptorPool(m_pDeviceContext->GetLogicalDevice(), g_DescriptorPool, g_Allocator);
}


void ImGuiPipeline::CleanupVulkanWindow()
{
	ImGuiDestroyWindow(m_pDeviceContext->GetInstance(), m_pDeviceContext->GetLogicalDevice(), g_Allocator);
}
}
