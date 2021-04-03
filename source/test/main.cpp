#include <vulkan/Renderer.h>
#include <vulkan/Model.h>
#include <vulkan/Pipeline.h>
#include <vulkan/Swapchain.h>
#include <vulkan/Window.h>

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <../glfw/include/GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_vulkan.h>


static VkInstance               g_Instance = VK_NULL_HANDLE;
static VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
static VkDevice                 g_Device = VK_NULL_HANDLE;
static uint32_t                 g_QueueFamily = (uint32_t)-1;
static VkQueue                  g_Queue = VK_NULL_HANDLE;
static VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

static ImGui_ImplVulkanH_Window g_MainWindowData {};
static int                      g_MinImageCount {2};
static bool                     g_SwapChainRebuild {false};

static VkAllocationCallbacks* g_Allocator = NULL;
static VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;


static void check_vk_result(VkResult err)
{
	if (err == VK_SUCCESS)
		return;

	std::cerr << "Vulkan error: vkResult = " << err << '\n';
	if (err < 0)
		abort();
}


void ImGuiCreateRenderPass(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext, std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain, VkRenderPass& imGuiRenderPass)
{
	VkAttachmentDescription colorAttachment {};
	colorAttachment.format = pSwapchain->GetImageFormat();
	colorAttachment.samples = pDeviceContext->GetMsaaSamples();
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef {};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	//subpass.pDepthStencilAttachment = nullptr;
	//subpass.pResolveAttachments = nullptr;

	VkSubpassDependency dependency {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(pDeviceContext->GetLogicalDevice(), &renderPassInfo, nullptr, &imGuiRenderPass) != VK_SUCCESS)
	{
		throw std::runtime_error("failed to create render pass");
	}
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
		VkCommandBufferBeginInfo info = {};
		info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
		check_vk_result(err);
	}
	{
		VkRenderPassBeginInfo info = {};
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
		VkSubmitInfo info = {};
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
	VkPresentInfoKHR info = {};
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
	VkDescriptorPoolCreateInfo pool_info = {};
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
	pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
	pool_info.pPoolSizes = pool_sizes;
	err = vkCreateDescriptorPool(g_Device, &pool_info, g_Allocator, &g_DescriptorPool);
	check_vk_result(err);
}


static void SetupVulkanWindow(std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext,
							  /*std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain,*/
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
	ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, wd, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
}


static void CleanupVulkan()
{
	vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);
}


static void CleanupVulkanWindow()
{
	ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
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
		//std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain = std::make_shared<Jettison::Renderer::Swapchain>(pDeviceContext);
		//std::shared_ptr<Jettison::Renderer::Pipeline> pPipeline = std::make_shared<Jettison::Renderer::Pipeline>(pDeviceContext, pSwapchain);
		//std::shared_ptr<Jettison::Renderer::Renderer> pRenderer = std::make_shared<Jettison::Renderer::Renderer>(pDeviceContext, pWindow, pSwapchain, pPipeline);

		pWindow->Init();
		GLFWwindow* window = pWindow->GetGLFWWindow();
		pDeviceContext->Init();
		//pSwapchain->Init();
		//pPipeline->Init();
		//pRenderer->Init();

		//Jettison::Renderer::Model model {pDeviceContext};
		//model.LoadModel();

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
		SetupVulkanWindow(pDeviceContext, wd, surface, w, h);

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		VkRenderPass imGuiRenderPass {VK_NULL_HANDLE};

		//ImGuiCreateRenderPass(pDeviceContext, pSwapchain, imGuiRenderPass);

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForVulkan(window, true);
		ImGui_ImplVulkan_InitInfo init_info = {};
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
			ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
			ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
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
					ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
					ImGui_ImplVulkanH_CreateOrResizeWindow(g_Instance, g_PhysicalDevice, g_Device, &g_MainWindowData, g_QueueFamily, g_Allocator, width, height, g_MinImageCount);
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

			//// HACK: We need to add the model command buffers to the pipeline.
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

		//model.Destroy();
		//pRenderer->Destroy();
		//pPipeline->Destroy();
		//pSwapchain->Destroy();
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