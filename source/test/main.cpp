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

		pImGuiPipeline->Init();

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
			pImGuiPipeline->ImGuiCreateOrResizeWindow(pDeviceContext->GetGraphicsQueueIndex(), pImGuiPipeline->g_Allocator);
		}

		while (!glfwWindowShouldClose(pWindow->GetGLFWWindow()))
		{
			glfwPollEvents();

			// Resize swap chain?
			if (pImGuiPipeline->g_SwapChainRebuild)
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
					pImGuiPipeline->ImGuiCreateOrResizeWindow(pDeviceContext->GetGraphicsQueueIndex(), pImGuiPipeline->g_Allocator);
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

			//pRenderer->Render();
			pImGuiPipeline->Render();

			// TODO: Need to add this in when I switch to the viewports build of ImGui.
			// Update and Render additional Platform Windows
			//if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
			//{
			//	ImGui::UpdatePlatformWindows();
			//	ImGui::RenderPlatformWindowsDefault();
			//}

			// Present Main Platform Window
			//pRenderer->Present();
			pImGuiPipeline->Present();
		}

		// Cleanup
		err = pDeviceContext->WaitIdle();
		check_vk_result(err);

		// ImGui shutdown.
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();

		pImGuiPipeline->CleanupVulkanWindow();
		pImGuiPipeline->CleanupVulkan();

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