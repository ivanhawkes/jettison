// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <vulkan/Model.h>
#include <vulkan/Pipeline.h>
#include <vulkan/Renderer.h>
#include <vulkan/Swapchain.h>
#include <vulkan/Window.h>
#include <vulkan/imgui/ImGuiRenderer.h>

using namespace Jettison::Renderer;


int Loop(bool smokeTest, const std::filesystem::path &assetDirectory)
{
	try
	{
		std::shared_ptr<Jettison::Renderer::Window> pWindow =
		    std::make_shared<Jettison::Renderer::Window>();
		std::shared_ptr<Jettison::Renderer::DeviceContext> pDeviceContext =
		    std::make_shared<Jettison::Renderer::DeviceContext>(pWindow);
		std::shared_ptr<Jettison::Renderer::Swapchain> pSwapchain =
		    std::make_shared<Jettison::Renderer::Swapchain>(pDeviceContext);
		std::shared_ptr<Jettison::Renderer::Pipeline> pPipeline =
		    std::make_shared<Jettison::Renderer::Pipeline>(pDeviceContext, pSwapchain, assetDirectory);
		std::shared_ptr<Jettison::Renderer::Renderer> pRenderer =
		    std::make_shared<Jettison::Renderer::Renderer>(
		        pDeviceContext, pWindow, pSwapchain, pPipeline);
		Jettison::Renderer::ImGuiRenderer imguiRenderer{pDeviceContext, assetDirectory};

		pWindow->Init();
		pDeviceContext->Init();
		pSwapchain->Init();

		Jettison::Renderer::Model model{pDeviceContext};
		model.LoadModel(assetDirectory / "models" / "viking_room.wobj");
		pPipeline->Init();
		pPipeline->CreateCommandBuffers();
		imguiRenderer.Init(
		    pWindow->GetGLFWWindow(), pPipeline->GetRenderPass(), pSwapchain->GetImageCount());
		pRenderer->Init();
		auto smokeStart = std::chrono::steady_clock::now();
		int smokeResizes = 0;
		int smokeFrames = 0;
		bool smokeResizePending = false;
		bool showImGui = smokeTest;
		bool f1WasDown = false;
		bool escapeWasDown = false;

		while (!glfwWindowShouldClose(pWindow->GetGLFWWindow()))
		{
			glfwPollEvents();
			const bool f1Down = glfwGetKey(pWindow->GetGLFWWindow(), GLFW_KEY_F1) == GLFW_PRESS;
			if (f1Down && !f1WasDown)
				showImGui = !showImGui;
			f1WasDown = f1Down;

			const bool escapeDown =
			    glfwGetKey(pWindow->GetGLFWWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS;
			if (escapeDown && !escapeWasDown)
			{
				if (showImGui)
					showImGui = false;
				else
					glfwSetWindowShouldClose(pWindow->GetGLFWWindow(), GLFW_TRUE);
			}
			escapeWasDown = escapeDown;

			imguiRenderer.BeginFrame();
			if (showImGui)
				imguiRenderer.ShowDemoWindow(&showImGui);
			imguiRenderer.EndFrame();

			const bool frameResized = pRenderer->Render(model, imguiRenderer);
			if (smokeTest && frameResized && smokeResizePending)
			{
				++smokeResizes;
				smokeFrames = 0;
				smokeResizePending = false;
			}

			if (smokeTest)
			{
				++smokeFrames;
				if (smokeResizes == 2 && smokeFrames >= 8)
					glfwSetWindowShouldClose(pWindow->GetGLFWWindow(), GLFW_TRUE);
				else if (smokeResizes < 2 && smokeFrames >= 8 && !smokeResizePending)
				{
					smokeResizePending = true;
					glfwSetWindowSize(
					    pWindow->GetGLFWWindow(), smokeResizes == 0 ? 1024 : 800,
					    smokeResizes == 0 ? 768 : 600);
				}
				if (std::chrono::steady_clock::now() - smokeStart > std::chrono::seconds(30))
					glfwSetWindowShouldClose(pWindow->GetGLFWWindow(), GLFW_TRUE);
			}
		}

		// Cleanup
		VkResult err = pDeviceContext->WaitIdle();
		check_vk_result(err);

		imguiRenderer.Destroy();
		pRenderer->Destroy();
		pPipeline->Destroy();
		model.Destroy();
		pSwapchain->Destroy();
		pDeviceContext->Destroy();
		pWindow->Destroy();
		if (smokeTest && (smokeResizes != 2 || smokeFrames < 8))
			throw std::runtime_error("Model rendering smoke test did not complete both window resizes");
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return 0;
}


int main(int argc, char **argv)
{
	bool smokeTest = false;
	std::filesystem::path executable = std::filesystem::absolute(argv[0]);
#ifdef __linux__
	std::error_code error;
	auto actualExecutable = std::filesystem::canonical("/proc/self/exe", error);
	if (!error)
		executable = actualExecutable;
#endif
	std::filesystem::path assetDirectory = executable.parent_path() / "assets";
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--smoke-test")
			smokeTest = true;
		else if (argument == "--assets" && index + 1 < argc)
			assetDirectory = std::filesystem::absolute(argv[++index]);
		else
		{
			std::cerr << "Usage: " << argv[0] << " [--smoke-test] [--assets <directory>]\n";
			return EXIT_FAILURE;
		}
	}
	return Loop(smokeTest, assetDirectory);
}