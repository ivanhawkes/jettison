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


int main()
{
	try
	{
		std::shared_ptr<Jettison::Renderer::Window> pWindow = std::make_shared<Jettison::Renderer::Window>();
		std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext = std::make_shared<Jettison::Renderer::DeviceContext>(pWindow);
		std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain = std::make_shared<Jettison::Renderer::Swapchain>(pDeviceContext);
		std::shared_ptr<Jettison::Renderer::Pipeline> pPipeline = std::make_shared<Jettison::Renderer::Pipeline>(pDeviceContext, pSwapchain);
		std::shared_ptr<Jettison::Renderer::Renderer> pRenderer = std::make_shared<Jettison::Renderer::Renderer>(pDeviceContext, pWindow, pSwapchain, pPipeline);

		pWindow->Init();
		pDeviceContext->Init();
		pSwapchain->Init();
		pPipeline->Init();
		pRenderer->Init();

		Jettison::Renderer::Model model {pDeviceContext};
		model.LoadModel();

		// HACK: We need to add the model command buffers to the pipeline.
		//pPipeline->CreateCommandBuffers(model);

		// TODO: InitImGui();

		while (!glfwWindowShouldClose(pWindow->GetGLFWWindow()))
		{
			glfwPollEvents();

			// TODO: ImGui before.

			// HACK: We need to add the model command buffers to the pipeline.
			pPipeline->CreateCommandBuffers(model);

			pRenderer->DrawFrame();

			// TODO: ImGui after.
		}

		pDeviceContext->WaitIdle();

		model.Destroy();
		pRenderer->Destroy();
		pPipeline->Destroy();
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