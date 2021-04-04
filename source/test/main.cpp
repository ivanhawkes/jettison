// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <../glfw/include/GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

#include <vulkan/Renderer.h>
#include <vulkan/Model.h>
#include <vulkan/Pipeline.h>
#include <vulkan/Swapchain.h>
#include <vulkan/Window.h>

#include <vulkan/imgui/imgui.h>
#include <vulkan/imgui/imgui_impl_glfw.h>
#include <vulkan/imgui/imgui_impl_vulkan.h>


static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData {};
static int                      g_MinImageCount {2};
static bool                     g_SwapChainRebuild {false};

static VkAllocationCallbacks*	g_Allocator = {nullptr};
static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;


// TODO: Remove this - it's a copy from the ImGui vulkan implementation.
// Reusable buffers used for rendering 1 current in-flight frame, for ImGui_ImplVulkan_RenderDrawData()
// [Please zero-clear before use!]
struct ImGui_ImplVulkanH_FrameRenderBuffers
{
	VkDeviceMemory      VertexBufferMemory;
	VkDeviceMemory      IndexBufferMemory;
	VkDeviceSize        VertexBufferSize;
	VkDeviceSize        IndexBufferSize;
	VkBuffer            VertexBuffer;
	VkBuffer            IndexBuffer;
};


// TODO: Remove this - it's a copy from the ImGui vulkan implementation.
// Each viewport will hold 1 ImGui_ImplVulkanH_WindowRenderBuffers
// [Please zero-clear before use!]
struct ImGui_ImplVulkanH_WindowRenderBuffers
{
	uint32_t            Index;
	uint32_t            Count;
	ImGui_ImplVulkanH_FrameRenderBuffers* FrameRenderBuffers;
};


static void check_vk_result(VkResult err)
{
	if (err == VK_SUCCESS)
		return;

	std::cerr << "Vulkan error: vkResult = " << err << '\n';
	if (err < 0)
		abort();
}


void ImGuiInitFontTexture(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext)
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
	VkCommandBuffer command_buffer = pDeviceContext->BeginSingleTimeCommands();
	ImGui_ImplVulkan_CreateFontsTexture(command_buffer);
	pDeviceContext->EndSingleTimeCommands(command_buffer);
}


static void FrameRender(/*std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,*/ ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data)
{
	VkResult err;

	VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
	VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	err = vkAcquireNextImageKHR(g_Device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &wd->FrameIndex);
	if (err == VK_ERROR_OUT_OF_DATE_KHR)
	{
		g_SwapChainRebuild = true;
		return;
	}
	check_vk_result(err);

	ImGui_ImplVulkanH_Frame* fd = &wd->Frames[wd->FrameIndex];
	{
		err = vkWaitForFences(g_Device, 1, &fd->Fence, VK_TRUE, UINT64_MAX);    // wait indefinitely instead of periodically checking
		check_vk_result(err);

		err = vkResetFences(g_Device, 1, &fd->Fence);
		check_vk_result(err);
	}
	{
		err = vkResetCommandPool(g_Device, fd->CommandPool, 0);
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
		info.renderPass = wd->RenderPass;
		info.framebuffer = fd->Framebuffer;
		info.renderArea.extent.width = wd->Width;
		info.renderArea.extent.height = wd->Height;
		info.clearValueCount = 1;
		info.pClearValues = &wd->ClearValue;
		vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
	}

	// Record dear imgui primitives into command buffer
	ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

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


static void FramePresent(/*std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,*/ ImGui_ImplVulkanH_Window* wd)
{
	if (g_SwapChainRebuild)
		return;
	VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
	VkPresentInfoKHR info {};
	info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	info.waitSemaphoreCount = 1;
	info.pWaitSemaphores = &render_complete_semaphore;
	info.swapchainCount = 1;
	info.pSwapchains = &wd->Swapchain;
	info.pImageIndices = &wd->FrameIndex;
	VkResult err = vkQueuePresentKHR(g_Queue, &info);
	if (err == VK_ERROR_OUT_OF_DATE_KHR)
	{
		g_SwapChainRebuild = true;
		return;
	}
	check_vk_result(err);
	wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->ImageCount; // Now we can use the next set of semaphores
}


void ImGuiCreateWindowCommandBuffers(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator)
{
	IM_ASSERT(physical_device != VK_NULL_HANDLE && device != VK_NULL_HANDLE);
	(void)physical_device;
	(void)allocator;

	// Create Command Buffers
	VkResult err;
	for (uint32_t i = 0; i < wd->ImageCount; i++)
	{
		ImGui_ImplVulkanH_Frame* fd = &wd->Frames[i];
		ImGui_ImplVulkanH_FrameSemaphores* fsd = &wd->FrameSemaphores[i];
		{
			VkCommandPoolCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			info.queueFamilyIndex = queue_family;
			err = vkCreateCommandPool(device, &info, allocator, &fd->CommandPool);
			check_vk_result(err);
		}
		{
			VkCommandBufferAllocateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.commandPool = fd->CommandPool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = 1;
			err = vkAllocateCommandBuffers(device, &info, &fd->CommandBuffer);
			check_vk_result(err);
		}
		{
			VkFenceCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			err = vkCreateFence(device, &info, allocator, &fd->Fence);
			check_vk_result(err);
		}
		{
			VkSemaphoreCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			err = vkCreateSemaphore(device, &info, allocator, &fsd->ImageAcquiredSemaphore);
			check_vk_result(err);
			err = vkCreateSemaphore(device, &info, allocator, &fsd->RenderCompleteSemaphore);
			check_vk_result(err);
		}
	}
}


int ImGuiGetMinImageCountFromPresentMode(VkPresentModeKHR present_mode)
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


void ImGuiDestroyFrame(VkDevice device, ImGui_ImplVulkanH_Frame* fd, const VkAllocationCallbacks* allocator)
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


void ImGuiDestroyFrameSemaphores(VkDevice device, ImGui_ImplVulkanH_FrameSemaphores* fsd, const VkAllocationCallbacks* allocator)
{
	vkDestroySemaphore(device, fsd->ImageAcquiredSemaphore, allocator);
	vkDestroySemaphore(device, fsd->RenderCompleteSemaphore, allocator);
	fsd->ImageAcquiredSemaphore = fsd->RenderCompleteSemaphore = VK_NULL_HANDLE;
}


void ImGuiDestroyWindow(VkInstance instance, VkDevice device, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator)
{
	// FIXME: We could wait on the Queue if we had the queue in wd-> (otherwise VulkanH functions can't use globals)
	vkDeviceWaitIdle(device);
	//vkQueueWaitIdle(g_Queue);

	for (uint32_t i = 0; i < wd->ImageCount; i++)
	{
		ImGuiDestroyFrame(device, &wd->Frames[i], allocator);
		ImGuiDestroyFrameSemaphores(device, &wd->FrameSemaphores[i], allocator);
	}

	IM_FREE(wd->Frames);
	IM_FREE(wd->FrameSemaphores);

	wd->Frames = nullptr;
	wd->FrameSemaphores = nullptr;

	vkDestroyPipeline(device, wd->Pipeline, allocator);
	vkDestroyRenderPass(device, wd->RenderPass, allocator);

	*wd = ImGui_ImplVulkanH_Window {};
}


void ImGuiDestroyFrameRenderBuffers(VkDevice device, ImGui_ImplVulkanH_FrameRenderBuffers* buffers, const VkAllocationCallbacks* allocator)
{
	if (buffers->VertexBuffer) { vkDestroyBuffer(device, buffers->VertexBuffer, allocator); buffers->VertexBuffer = VK_NULL_HANDLE; }
	if (buffers->VertexBufferMemory) { vkFreeMemory(device, buffers->VertexBufferMemory, allocator); buffers->VertexBufferMemory = VK_NULL_HANDLE; }
	if (buffers->IndexBuffer) { vkDestroyBuffer(device, buffers->IndexBuffer, allocator); buffers->IndexBuffer = VK_NULL_HANDLE; }
	if (buffers->IndexBufferMemory) { vkFreeMemory(device, buffers->IndexBufferMemory, allocator); buffers->IndexBufferMemory = VK_NULL_HANDLE; }
	buffers->VertexBufferSize = 0;
	buffers->IndexBufferSize = 0;
}


void ImGuiDestroyWindowRenderBuffers(VkDevice device, ImGui_ImplVulkanH_WindowRenderBuffers* buffers, const VkAllocationCallbacks* allocator)
{
	for (uint32_t n = 0; n < buffers->Count; n++)
		ImGuiDestroyFrameRenderBuffers(device, &buffers->FrameRenderBuffers[n], allocator);
	IM_FREE(buffers->FrameRenderBuffers);
	buffers->FrameRenderBuffers = nullptr;
	buffers->Index = 0;
	buffers->Count = 0;
}


// Also destroy old swap chain and in-flight frames data, if any.
void ImGuiCreateWindowSwapChain(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	VkPhysicalDevice physical_device, VkDevice device, ImGui_ImplVulkanH_Window* wd,
	const VkAllocationCallbacks* allocator, int w, int h, uint32_t min_image_count)
{
	VkResult err;

	//VkSwapchainKHR old_swapchain = wd->Swapchain;
	//wd->Swapchain = nullptr;

	err = vkDeviceWaitIdle(device);
	check_vk_result(err);


	// TODO: *** Figure out why the screen doesn't update after a resize - is it the extents again?


	// We don't use ImGuiDestroyWindow() because we want to preserve the old swapchain to create the new one.
	// Destroy old Framebuffer
	for (uint32_t i = 0; i < wd->ImageCount; i++)
	{
		ImGuiDestroyFrame(device, &wd->Frames[i], allocator);
		ImGuiDestroyFrameSemaphores(device, &wd->FrameSemaphores[i], allocator);
	}
	IM_FREE(wd->Frames);
	IM_FREE(wd->FrameSemaphores);
	wd->Frames = nullptr;
	wd->FrameSemaphores = nullptr;
	wd->ImageCount = 0;
	if (wd->RenderPass)
		vkDestroyRenderPass(device, wd->RenderPass, allocator);
	if (wd->Pipeline)
		vkDestroyPipeline(device, wd->Pipeline, allocator);

	// If min image count was not specified, request different count of images dependent on selected present mode
	if (min_image_count == 0)
		min_image_count = ImGuiGetMinImageCountFromPresentMode(wd->PresentMode);

	// Create Swapchain
	{
		//VkSwapchainCreateInfoKHR info {};
		//info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		//info.surface = wd->Surface;
		//info.minImageCount = min_image_count;
		//info.imageFormat = wd->SurfaceFormat.format;
		//info.imageColorSpace = wd->SurfaceFormat.colorSpace;
		//info.imageArrayLayers = 1;
		//info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		//info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;           // Assume that graphics family == present family
		//info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		//info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		//info.presentMode = wd->PresentMode;
		//info.clipped = VK_TRUE;
		//info.oldSwapchain = old_swapchain;
		//
		//VkSurfaceCapabilitiesKHR cap;
		//err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, wd->Surface, &cap);
		//check_vk_result(err);
		//
		//if (info.minImageCount < cap.minImageCount)
		//	info.minImageCount = cap.minImageCount;
		//else if (cap.maxImageCount != 0 && info.minImageCount > cap.maxImageCount)
		//	info.minImageCount = cap.maxImageCount;

		//if (cap.currentExtent.width == 0xffffffff)
		//{
		//	info.imageExtent.width = wd->Width = w;
		//	info.imageExtent.height = wd->Height = h;
		//}
		//else
		//{
		//	info.imageExtent.width = wd->Width = cap.currentExtent.width;
		//	info.imageExtent.height = wd->Height = cap.currentExtent.height;
		//}
		//
		//err = vkCreateSwapchainKHR(device, &info, allocator, &wd->Swapchain);
		//check_vk_result(err);

		wd->Swapchain = pSwapchain->GetVkSwapchainHandle();
		wd->ImageCount = pSwapchain->GetImageCount();
		wd->Height = pSwapchain->GetExtents().height;
		wd->Width = pSwapchain->GetExtents().width;

		err = vkGetSwapchainImagesKHR(device, wd->Swapchain, &wd->ImageCount, nullptr);
		check_vk_result(err);

		VkImage backbuffers[16] {};
		IM_ASSERT(wd->ImageCount >= min_image_count);
		IM_ASSERT(wd->ImageCount < IM_ARRAYSIZE(backbuffers));
		err = vkGetSwapchainImagesKHR(device, wd->Swapchain, &wd->ImageCount, backbuffers);
		check_vk_result(err);

		//auto imageCount = pSwapchain->GetImageCount();
		//std::vector<VkImage> swapchainImages {};
		//swapchainImages.resize(imageCount);

		//vkGetSwapchainImagesKHR(pDeviceContext->GetLogicalDevice(), pSwapchain->GetVkSwapchainHandle(), pSwapchain->GetImageCountAddress(), nullptr);
		//vkGetSwapchainImagesKHR(pDeviceContext->GetLogicalDevice(), pSwapchain->GetVkSwapchainHandle(), pSwapchain->GetImageCountAddress(), swapchainImages.data());

		IM_ASSERT(wd->Frames == nullptr);
		wd->Frames = (ImGui_ImplVulkanH_Frame*)IM_ALLOC(sizeof(ImGui_ImplVulkanH_Frame) * wd->ImageCount);
		wd->FrameSemaphores = (ImGui_ImplVulkanH_FrameSemaphores*)IM_ALLOC(sizeof(ImGui_ImplVulkanH_FrameSemaphores) * wd->ImageCount);
		memset(wd->Frames, 0, sizeof(wd->Frames[0]) * wd->ImageCount);
		memset(wd->FrameSemaphores, 0, sizeof(wd->FrameSemaphores[0]) * wd->ImageCount);
		for (uint32_t i = 0; i < wd->ImageCount; i++)
			wd->Frames[i].Backbuffer = backbuffers[i];
	}

	//if (old_swapchain)
	//{
	//	vkDestroySwapchainKHR(device, old_swapchain, allocator);
	//}

	// Create the Render Pass
	{
		VkAttachmentDescription attachmentDescription {};
		attachmentDescription.format = wd->SurfaceFormat.format;
		attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription.loadOp = wd->ClearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
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
		err = vkCreateRenderPass(device, &renderPassInfo, allocator, &wd->RenderPass);
		check_vk_result(err);

		// We do not create a pipeline by default as this is also used by examples' main.cpp,
		// but secondary viewport in multi-viewport mode may want to create one with:
		//ImGui_ImplVulkan_CreatePipeline(device, allocator, VK_NULL_HANDLE, wd->RenderPass, VK_SAMPLE_COUNT_1_BIT, &wd->Pipeline, g_Subpass);
	}

	// Create The Image Views
	{
		VkImageViewCreateInfo info {};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = wd->SurfaceFormat.format;
		info.components.r = VK_COMPONENT_SWIZZLE_R;
		info.components.g = VK_COMPONENT_SWIZZLE_G;
		info.components.b = VK_COMPONENT_SWIZZLE_B;
		info.components.a = VK_COMPONENT_SWIZZLE_A;
		VkImageSubresourceRange image_range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		info.subresourceRange = image_range;
		for (uint32_t i = 0; i < wd->ImageCount; i++)
		{
			ImGui_ImplVulkanH_Frame* fd = &wd->Frames[i];
			info.image = fd->Backbuffer;
			err = vkCreateImageView(device, &info, allocator, &fd->BackbufferView);
			check_vk_result(err);
		}
	}

	// Create Framebuffer
	{
		VkImageView attachment[1];
		VkFramebufferCreateInfo info {};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.renderPass = wd->RenderPass;
		info.attachmentCount = 1;
		info.pAttachments = attachment;
		info.width = wd->Width;
		info.height = wd->Height;
		info.layers = 1;
		for (uint32_t i = 0; i < wd->ImageCount; i++)
		{
			ImGui_ImplVulkanH_Frame* fd = &wd->Frames[i];
			attachment[0] = fd->BackbufferView;
			err = vkCreateFramebuffer(device, &info, allocator, &fd->Framebuffer);
			check_vk_result(err);
		}
	}
}


// Create or resize window
void ImGuiCreateOrResizeWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	VkInstance instance, VkPhysicalDevice physical_device, VkDevice device,
	ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator, int width, int height, uint32_t min_image_count)
{
	ImGuiCreateWindowSwapChain(pDeviceContext, pSwapchain, physical_device, device, wd, allocator, width, height, min_image_count);
	ImGuiCreateWindowCommandBuffers(pDeviceContext, pSwapchain, physical_device, device, wd, queue_family, allocator);
}


static void SetupVulkan(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext)
{
	VkResult err;

	g_Instance = pDeviceContext->GetInstance();
	g_PhysicalDevice = pDeviceContext->GetPhysicalDevice();
	g_QueueFamily = pDeviceContext->GetGraphicsQueueIndex();

	// We require a logical device with 1 queue.
	g_Device = pDeviceContext->GetLogicalDevice();
	vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);

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
	err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
	check_vk_result(err);
}


static void SetupVulkanWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface, int width, int height)
{
	wd->Surface = surface;

	// Check for WSI support
	VkBool32 res;
	vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, wd->Surface, &res);
	if (res != VK_TRUE)
	{
		fprintf(stderr, "Error no WSI support on physical device 0\n");
		exit(-1);
	}

	// Select surface format.
	const std::vector<VkFormat> requestSurfaceImageFormat {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
	const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
	wd->SurfaceFormat = pDeviceContext->FindSupportedSurfaceFormat(requestSurfaceImageFormat, requestSurfaceColorSpace);

	// Select present mode.
#ifdef IMGUI_UNLIMITED_FRAME_RATE
	std::vector<VkPresentModeKHR> presentModeCandiates {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
#else
	std::vector<VkPresentModeKHR> presentModeCandiates {VK_PRESENT_MODE_FIFO_KHR};
#endif
	wd->PresentMode = pDeviceContext->FindSupportedPresentMode(presentModeCandiates);

	// Create SwapChain, RenderPass, Framebuffer, etc.
	IM_ASSERT(g_MinImageCount >= 2);
	ImGuiCreateOrResizeWindow(pDeviceContext, pSwapchain, g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
}


static void CleanupVulkan()
{
	vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);
}


static void CleanupVulkanWindow()
{
	ImGuiDestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}


int main()
{
	bool showDemoWindow = true;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	VkDescriptorPool imGuiDescriptorPool {VK_NULL_HANDLE};
	VkResult err {VK_SUCCESS};

	try
	{
		std::shared_ptr<Jettison::Renderer::Window> pWindow = std::make_shared<Jettison::Renderer::Window>();
		std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext = std::make_shared<Jettison::Renderer::DeviceContext>(pWindow);
		std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain = std::make_shared<Jettison::Renderer::Swapchain>(pDeviceContext);
		//std::shared_ptr<Jettison::Renderer::Pipeline> pPipeline = std::make_shared<Jettison::Renderer::Pipeline>(pDeviceContext, pSwapchain);
		//std::shared_ptr<Jettison::Renderer::Renderer> pRenderer = std::make_shared<Jettison::Renderer::Renderer>(pDeviceContext, pWindow, pSwapchain, pPipeline);

		pWindow->Init();
		GLFWwindow* window = pWindow->GetGLFWWindow();
		pDeviceContext->Init();
		pSwapchain->Init();
		//pPipeline->Init();
		//pRenderer->Init();

		Jettison::Renderer::Model model {pDeviceContext};
		model.LoadModel();

		// Setup Vulkan
		if (!glfwVulkanSupported())
		{
			printf("GLFW: Vulkan Not Supported\n");
			return 1;
		}
		SetupVulkan(pDeviceContext);

		// Create Window Surface
		VkSurfaceKHR surface = pDeviceContext->GetSurface();

		// Create Framebuffers
		int w, h;
		glfwGetFramebufferSize(window, &w, &h);
		ImGui_ImplVulkanH_Window* wd = &g_MainWindowData;
		SetupVulkanWindow(pDeviceContext, pSwapchain, wd, surface, w, h);

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForVulkan(window, true);
		ImGui_ImplVulkan_InitInfo init_info {};
		init_info.Instance = g_Instance;
		init_info.PhysicalDevice = g_PhysicalDevice;
		init_info.Device = g_Device;
		init_info.QueueFamily = g_QueueFamily;
		init_info.Queue = g_Queue;
		init_info.PipelineCache = g_PipelineCache;
		init_info.DescriptorPool = g_DescriptorPool;
		init_info.Allocator = g_Allocator;
		init_info.MinImageCount = g_MinImageCount;
		init_info.ImageCount = wd->ImageCount;
		init_info.CheckVkResultFn = check_vk_result;
		ImGui_ImplVulkan_Init(&init_info, wd->RenderPass);

		// Load the font list into the font texture.
		ImGuiInitFontTexture(pDeviceContext);

		int width;
		int height;
		glfwGetFramebufferSize(pWindow->GetGLFWWindow(), &width, &height);
		if (width > 0 && height > 0)
		{
			// The swapchain must be recreated for the next extents to apply along with any other possible changes.
			pSwapchain->Recreate();
			//pPipeline->Recreate();

			ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
			ImGuiCreateOrResizeWindow(pDeviceContext, pSwapchain, g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
			g_MainWindowData.FrameIndex = 0;
			g_SwapChainRebuild = false;
		}

		while (!glfwWindowShouldClose(pWindow->GetGLFWWindow()))
		{
			glfwPollEvents();

			// Resize swap chain?
			if (g_SwapChainRebuild)
			{
				int width, height;
				glfwGetFramebufferSize(window, &width, &height);
				if (width > 0 && height > 0)
				{
					// The swapchain must be recreated for the next extents to apply along with any other possible changes.
					pSwapchain->Recreate();
					//pPipeline->Recreate();

					ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
					ImGuiCreateOrResizeWindow(pDeviceContext, pSwapchain, g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
					g_MainWindowData.FrameIndex = 0;
					g_SwapChainRebuild = false;
				}
			}

			// Start the Dear ImGui frame
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			// Show the demo window if requested.
			if (showDemoWindow)
				ImGui::ShowDemoWindow(&showDemoWindow);

			// HACK: We need to add the model command buffers to the pipeline.
			//pPipeline->CreateCommandBuffers(model);

			//pRenderer->DrawFrame();

			// Rendering
			ImGui::Render();
			ImDrawData* main_draw_data = ImGui::GetDrawData();
			const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
			memcpy(&wd->ClearValue.color.float32[0], &clear_color, 4 * sizeof(float));
			if (!main_is_minimized)
				FrameRender(wd, main_draw_data);


			// Update and Render additional Platform Windows
			//if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			//{
			//	ImGui::UpdatePlatformWindows();
			//	ImGui::RenderPlatformWindowsDefault();
			//}

			// Present Main Platform Window
			//if (!main_is_minimized)
			//	FramePresent(pDeviceContext, wd);
			if (!main_is_minimized)
				FramePresent(wd);
		}

		// Cleanup
		//err = pDeviceContext->WaitIdle();
		err = vkDeviceWaitIdle(g_Device);
		check_vk_result(err);

		// ImGui shutdown.
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		CleanupVulkanWindow();
		CleanupVulkan();

		model.Destroy();
		//pRenderer->Destroy();
		//pPipeline->Destroy();
		pSwapchain->Destroy();
		pDeviceContext->Destroy();
		pWindow->Destroy();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return 0;
}