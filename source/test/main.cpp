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

#include <vulkan/imgui/ImGuiPipeline.h>
#include <vulkan/imgui/ImGuiRenderer.h>


using namespace Jettison::Renderer;


int Loop()
{
	bool showDemoWindow = true;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	VkResult err {VK_SUCCESS};

	try
	{
		std::shared_ptr<Jettison::Renderer::Window> pWindow = std::make_shared<Jettison::Renderer::Window>();
		std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext = std::make_shared<Jettison::Renderer::DeviceContext>(pWindow);
		std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain = std::make_shared<Jettison::Renderer::Swapchain>(pDeviceContext);
		//std::shared_ptr<Jettison::Renderer::Pipeline> pPipeline = std::make_shared<Jettison::Renderer::Pipeline>(pDeviceContext, pSwapchain);
		//std::shared_ptr<Jettison::Renderer::Renderer> pRenderer = std::make_shared<Jettison::Renderer::Renderer>(pDeviceContext, pWindow, pSwapchain, pPipeline);
		
		std::shared_ptr<Jettison::Renderer::ImGuiPipeline> pImGuiPipeline = std::make_shared<Jettison::Renderer::ImGuiPipeline>(pDeviceContext, pSwapchain);

		pWindow->Init();
		pDeviceContext->Init();
		pSwapchain->Init();
		//pPipeline->Init();
		//pRenderer->Init();

		Jettison::Renderer::Model model {pDeviceContext};
		model.LoadModel();

		// TODO: Ease out of using this crutch.
		ImGui_ImplVulkanH_Window* wd = &g_windowData;

		pImGuiPipeline->Init();

		// Setup Vulkan
		SetupVulkan(pDeviceContext);

		// Create Framebuffers
		SetupVulkanWindow(pDeviceContext, pSwapchain, wd);

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();

		// Setup Platform/Renderer bindings
		ImGui_ImplGlfw_InitForVulkan(pDeviceContext->GetWindow()->GetGLFWWindow(), true);
		ImGui_ImplVulkan_InitInfo init_info {};
		init_info.Instance = pDeviceContext->GetInstance();
		init_info.PhysicalDevice = pDeviceContext->GetPhysicalDevice();
		init_info.Device = pDeviceContext->GetLogicalDevice();
		init_info.QueueFamily = pDeviceContext->GetGraphicsQueueIndex();
		init_info.Queue = g_Queue;
		init_info.PipelineCache = g_PipelineCache;
		init_info.DescriptorPool = g_DescriptorPool;
		init_info.Allocator = g_Allocator;
		init_info.MinImageCount = pSwapchain->GetImageCount();
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
			pImGuiPipeline->Recreate();

			ImGui_ImplVulkan_SetMinImageCount(pSwapchain->GetImageCount());
			ImGuiCreateOrResizeWindow(pDeviceContext, pSwapchain, 
				wd, pDeviceContext->GetGraphicsQueueIndex(), g_Allocator);
			g_windowData.FrameIndex = 0;
			g_SwapChainRebuild = false;
		}

		while (!glfwWindowShouldClose(pWindow->GetGLFWWindow()))
		{
			glfwPollEvents();

			// Resize swap chain?
			if (g_SwapChainRebuild)
			{
				int width, height;
				glfwGetFramebufferSize(pDeviceContext->GetWindow()->GetGLFWWindow(), &width, &height);
				if (width > 0 && height > 0)
				{
					// The swapchain must be recreated for the next extents to apply along with any other possible changes.
					pSwapchain->Recreate();
					//pPipeline->Recreate();
					pImGuiPipeline->Recreate();

					ImGui_ImplVulkan_SetMinImageCount(pSwapchain->GetImageCount());
					ImGuiCreateOrResizeWindow(pDeviceContext, pSwapchain, 
						wd, pDeviceContext->GetGraphicsQueueIndex(), g_Allocator);
					g_windowData.FrameIndex = 0;
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
		err = pDeviceContext->WaitIdle();
		check_vk_result(err);

		// ImGui shutdown.
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		CleanupVulkanWindow();
		CleanupVulkan();

		pImGuiPipeline->Destroy();
		
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


int main()
{
	return Loop();
}