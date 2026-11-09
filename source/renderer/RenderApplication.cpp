#include "RenderApplication.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <vulkan/Pipeline.h>
#include <vulkan/Renderer.h>
#include <vulkan/Swapchain.h>
#include <vulkan/Window.h>
#include <vulkan/imgui/ImGuiRenderer.h>

namespace Jettison::Renderer
{
namespace
{
int Loop(
	bool smokeTest, const std::filesystem::path &assetDirectory,
	const std::filesystem::path &scenePath, const std::filesystem::path &saveScenePath,
	const char *windowTitle)
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
		glfwSetWindowTitle(pWindow->GetGLFWWindow(), windowTitle);
		pDeviceContext->Init();
		pSwapchain->Init();

		Scene scene{pDeviceContext};
		scene.Load(scenePath);
		if (!saveScenePath.empty())
			scene.Save(saveScenePath);
		const SceneUpdateSystem sceneUpdateSystem;
		const SceneRenderSystem sceneRenderSystem;
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
		auto previousFrame = std::chrono::steady_clock::now();

		while (!glfwWindowShouldClose(pWindow->GetGLFWWindow()))
		{
			const auto currentFrame = std::chrono::steady_clock::now();
			const float deltaTime = std::chrono::duration<float>(currentFrame - previousFrame).count();
			previousFrame = currentFrame;
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

			const bool frameResized = pRenderer->Render(
			    scene, sceneUpdateSystem, sceneRenderSystem, deltaTime, imguiRenderer);
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
		scene.Destroy();
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
}

int RunRenderApplication(
	int argc, char **argv, const char *windowTitle,
	const std::filesystem::path &assetSubdirectory)
{
	bool smokeTest = false;
	std::filesystem::path scenePath;
	std::filesystem::path saveScenePath;
	std::filesystem::path executable = std::filesystem::absolute(argv[0]);
#ifdef __linux__
	std::error_code error;
	auto actualExecutable = std::filesystem::canonical("/proc/self/exe", error);
	if (!error)
		executable = actualExecutable;
#endif
	std::filesystem::path assetDirectory = executable.parent_path() / assetSubdirectory;
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--smoke-test")
			smokeTest = true;
		else if (argument == "--assets" && index + 1 < argc)
			assetDirectory = std::filesystem::absolute(argv[++index]);
		else if (argument == "--scene" && index + 1 < argc)
			scenePath = std::filesystem::absolute(argv[++index]);
		else if (argument == "--save-scene" && index + 1 < argc)
			saveScenePath = std::filesystem::absolute(argv[++index]);
		else
		{
			std::cerr << "Usage: " << argv[0]
			          << " [--smoke-test] [--assets <directory>] [--scene <file>]"
			             " [--save-scene <file>]\n";
			return EXIT_FAILURE;
		}
	}
	if (scenePath.empty())
		scenePath = assetDirectory / "scene" / "main.yaml";
	return Loop(smokeTest, assetDirectory, scenePath, saveScenePath, windowTitle);
}
}