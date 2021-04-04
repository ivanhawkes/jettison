#pragma once

#include <vulkan/vulkan.h>

// STD.
#include <iostream>
#include <stdexcept>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "../DeviceContext.h"
#include "../Swapchain.h"


namespace Jettison::Renderer
{
static VkInstance               g_Instance {VK_NULL_HANDLE};
static VkPhysicalDevice         g_PhysicalDevice {VK_NULL_HANDLE};
static VkDevice                 g_Device {VK_NULL_HANDLE};
static uint32_t                 g_QueueFamily {(uint32_t)-1};
static VkQueue                  g_Queue {VK_NULL_HANDLE};
static VkDescriptorPool         g_DescriptorPool {VK_NULL_HANDLE};

static ImGui_ImplVulkanH_Window g_windowData {};
static bool                     g_SwapChainRebuild {false};

static VkAllocationCallbacks* g_Allocator {nullptr};
static VkPipelineCache          g_PipelineCache {VK_NULL_HANDLE};


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


static void ImGuiInitFontTexture(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext)
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


static int ImGuiGetMinImageCountFromPresentMode(VkPresentModeKHR present_mode)
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


static void ImGuiDestroyFrame(VkDevice device, ImGui_ImplVulkanH_Frame* fd, const VkAllocationCallbacks* allocator)
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


static void ImGuiDestroyFrameSemaphores(VkDevice device, ImGui_ImplVulkanH_FrameSemaphores* fsd, const VkAllocationCallbacks* allocator)
{
	vkDestroySemaphore(device, fsd->ImageAcquiredSemaphore, allocator);
	vkDestroySemaphore(device, fsd->RenderCompleteSemaphore, allocator);
	fsd->ImageAcquiredSemaphore = fsd->RenderCompleteSemaphore = VK_NULL_HANDLE;
}


static void ImGuiDestroyWindow(VkInstance instance, VkDevice device, ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator)
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


static void ImGuiDestroyFrameRenderBuffers(VkDevice device, ImGui_ImplVulkanH_FrameRenderBuffers* buffers, const VkAllocationCallbacks* allocator)
{
	if (buffers->VertexBuffer) { vkDestroyBuffer(device, buffers->VertexBuffer, allocator); buffers->VertexBuffer = VK_NULL_HANDLE; }
	if (buffers->VertexBufferMemory) { vkFreeMemory(device, buffers->VertexBufferMemory, allocator); buffers->VertexBufferMemory = VK_NULL_HANDLE; }
	if (buffers->IndexBuffer) { vkDestroyBuffer(device, buffers->IndexBuffer, allocator); buffers->IndexBuffer = VK_NULL_HANDLE; }
	if (buffers->IndexBufferMemory) { vkFreeMemory(device, buffers->IndexBufferMemory, allocator); buffers->IndexBufferMemory = VK_NULL_HANDLE; }
	buffers->VertexBufferSize = 0;
	buffers->IndexBufferSize = 0;
}


static void ImGuiDestroyWindowRenderBuffers(VkDevice device, ImGui_ImplVulkanH_WindowRenderBuffers* buffers, const VkAllocationCallbacks* allocator)
{
	for (uint32_t n = 0; n < buffers->Count; n++)
		ImGuiDestroyFrameRenderBuffers(device, &buffers->FrameRenderBuffers[n], allocator);
	IM_FREE(buffers->FrameRenderBuffers);
	buffers->FrameRenderBuffers = nullptr;
	buffers->Index = 0;
	buffers->Count = 0;
}


// Also destroy old swap chain and in-flight frames data, if any.
static void ImGuiCreateWindowSwapChain(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	ImGui_ImplVulkanH_Window* wd,
	const VkAllocationCallbacks* allocator)
{
	VkResult err;

	//VkSwapchainKHR old_swapchain = wd->Swapchain;
	//wd->Swapchain = nullptr;

	err = vkDeviceWaitIdle(pDeviceContext->GetLogicalDevice());
	check_vk_result(err);


	// TODO: *** Figure out why the screen doesn't update after a resize - is it the extents again?


	// We don't use ImGuiDestroyWindow() because we want to preserve the old swapchain to create the new one.
	// Destroy old Framebuffer
	for (uint32_t i = 0; i < wd->ImageCount; i++)
	{
		ImGuiDestroyFrame(pDeviceContext->GetLogicalDevice(), &wd->Frames[i], allocator);
		ImGuiDestroyFrameSemaphores(pDeviceContext->GetLogicalDevice(), &wd->FrameSemaphores[i], allocator);
	}
	IM_FREE(wd->Frames);
	IM_FREE(wd->FrameSemaphores);
	wd->Frames = nullptr;
	wd->FrameSemaphores = nullptr;
	wd->ImageCount = 0;
	if (wd->RenderPass)
		vkDestroyRenderPass(pDeviceContext->GetLogicalDevice(), wd->RenderPass, allocator);
	if (wd->Pipeline)
		vkDestroyPipeline(pDeviceContext->GetLogicalDevice(), wd->Pipeline, allocator);

	// Create Swapchain
	{
		wd->Swapchain = pSwapchain->GetVkSwapchainHandle();
		wd->ImageCount = pSwapchain->GetImageCount();
		wd->Height = pSwapchain->GetExtents().height;
		wd->Width = pSwapchain->GetExtents().width;

		err = vkGetSwapchainImagesKHR(pDeviceContext->GetLogicalDevice(), wd->Swapchain, &wd->ImageCount, nullptr);
		check_vk_result(err);

		VkImage backbuffers[16] {};
		IM_ASSERT(wd->ImageCount >= pSwapchain->GetImageCount());
		IM_ASSERT(wd->ImageCount < IM_ARRAYSIZE(backbuffers));
		err = vkGetSwapchainImagesKHR(pDeviceContext->GetLogicalDevice(), wd->Swapchain, &wd->ImageCount, backbuffers);
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
		err = vkCreateRenderPass(pDeviceContext->GetLogicalDevice(), &renderPassInfo, allocator, &wd->RenderPass);
		check_vk_result(err);

		// We do not create a pipeline by default as this is also used by examples' main.cpp,
		// but secondary viewport in multi-viewport mode may want to create one with:
		//ImGui_ImplVulkan_CreatePipeline(pDeviceContext->GetLogicalDevice(), allocator, VK_NULL_HANDLE, wd->RenderPass, VK_SAMPLE_COUNT_1_BIT, &wd->Pipeline, g_Subpass);
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
			err = vkCreateImageView(pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->BackbufferView);
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
			err = vkCreateFramebuffer(pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->Framebuffer);
			check_vk_result(err);
		}
	}
}


static void ImGuiCreateWindowCommandBuffers(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,
	std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	ImGui_ImplVulkanH_Window* wd, const VkAllocationCallbacks* allocator)
{
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
			info.queueFamilyIndex = pDeviceContext->GetGraphicsQueueIndex();
			err = vkCreateCommandPool(pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->CommandPool);
			check_vk_result(err);
		}
		{
			VkCommandBufferAllocateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			info.commandPool = fd->CommandPool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = 1;
			err = vkAllocateCommandBuffers(pDeviceContext->GetLogicalDevice(), &info, &fd->CommandBuffer);
			check_vk_result(err);
		}
		{
			VkFenceCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			err = vkCreateFence(pDeviceContext->GetLogicalDevice(), &info, allocator, &fd->Fence);
			check_vk_result(err);
		}
		{
			VkSemaphoreCreateInfo info {};
			info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			err = vkCreateSemaphore(pDeviceContext->GetLogicalDevice(), &info, allocator, &fsd->ImageAcquiredSemaphore);
			check_vk_result(err);
			err = vkCreateSemaphore(pDeviceContext->GetLogicalDevice(), &info, allocator, &fsd->RenderCompleteSemaphore);
			check_vk_result(err);
		}
	}
}


// Create or resize window
static void ImGuiCreateOrResizeWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	ImGui_ImplVulkanH_Window* wd, uint32_t queue_family, const VkAllocationCallbacks* allocator)
{
	ImGuiCreateWindowSwapChain(pDeviceContext, pSwapchain, wd, allocator);
	ImGuiCreateWindowCommandBuffers(pDeviceContext, pSwapchain, wd, allocator);
}


static void SetupVulkan(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext)
{
	VkResult err;

	g_Instance = pDeviceContext->GetInstance();
	g_PhysicalDevice = pDeviceContext->GetPhysicalDevice();
	g_QueueFamily = pDeviceContext->GetGraphicsQueueIndex();

	// We require a logical device with 1 queue.
	g_Device = pDeviceContext->GetLogicalDevice();
	vkGetDeviceQueue(g_Device, pDeviceContext->GetGraphicsQueueIndex(), 0, &g_Queue);

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


static void SetupVulkanWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,
	ImGui_ImplVulkanH_Window* wd)
{
	wd->Surface = pDeviceContext->GetSurface();

	// Check for WSI support
	VkBool32 isSupported;
	vkGetPhysicalDeviceSurfaceSupportKHR(pDeviceContext->GetPhysicalDevice(), pDeviceContext->GetGraphicsQueueIndex(), wd->Surface, &isSupported);
	if (isSupported != VK_TRUE)
	{
		throw std::runtime_error("No WSI support on physical device.");
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
	ImGuiCreateOrResizeWindow(pDeviceContext, pSwapchain, 
		wd, pDeviceContext->GetGraphicsQueueIndex(),
		g_Allocator);
}


static void CleanupVulkan()
{
	vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);
}


static void CleanupVulkanWindow()
{
	ImGuiDestroyWindow(g_Instance, g_Device, &g_windowData, g_Allocator);
}


class ImGuiPipeline
{
public:
	ImGuiPipeline(std::shared_ptr<DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain)
		:m_pDeviceContext {pDeviceContext}, m_pSwapchain {pSwapchain} {}

	// Disable copying.
	ImGuiPipeline() = default;
	ImGuiPipeline(const ImGuiPipeline&) = delete;
	ImGuiPipeline& operator=(const ImGuiPipeline&) = delete;

	void Init();

	void Recreate();

	void Destroy();

private:
	void Create();

	// Vulkan device context.
	std::shared_ptr<DeviceContext> m_pDeviceContext {nullptr};

	// Swapchain.
	std::shared_ptr<Jettison::Renderer::Swapchain> m_pSwapchain {nullptr};
};
}
