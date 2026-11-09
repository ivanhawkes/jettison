#include "RenderApplication.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
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
    bool smokeTest, const std::filesystem::path &assetDirectory, const std::filesystem::path &scenePath,
    const std::filesystem::path &saveScenePath, const char *windowTitle)
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
		// Software rendering is fill-rate bound, and the window opens at 1920x1080. The smoke
		// sequence is six stages of a handful of frames, so it starts on a small window: every
		// frame after this costs a quarter of what the opening size costs, and the resize stages
		// still change the size.
		if (smokeTest)
			glfwSetWindowSize(pWindow->GetGLFWWindow(), 640, 480);
		auto smokeStart = std::chrono::steady_clock::now();
		int smokeResizes = 0;
		int smokeFrames = 0;
		int smokeStage = 0;
		int smokeReloads = 0;
		bool smokeActionPending = false;
		const bool smokeCanIconify = glfwGetPlatform() == GLFW_PLATFORM_X11;
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

			const bool escapeDown = glfwGetKey(pWindow->GetGLFWWindow(), GLFW_KEY_ESCAPE) == GLFW_PRESS;
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
			if (smokeTest && frameResized && smokeActionPending && smokeStage <= 1)
			{
				++smokeResizes;
				smokeActionPending = false;
				smokeFrames = 0;
			}
			else if (smokeTest && smokeActionPending && smokeFrames >= 8)
			{
				// A window manager owns the geometry: on a tiling compositor (niri, which is what
				// this is developed under, through its XWayland) an X11 resize request is a request,
				// and the tiler answers it with the tile it has chosen. When that is the size the
				// window already has, no framebuffer resize ever arrives. The sequence moves on rather
				// than parking until the watchdog fires, and a resize that was refused is not counted
				// as one the engine handled. Under xvfb-run with no window manager, which is where CI
				// runs it, both requests land.
				smokeActionPending = false;
				smokeFrames = 0;
			}

			// The smoke sequence walks the paths that have hurt this engine: two resizes (a
			// swapchain recreate), a minimize and restore (a surface that reports no extent), and
			// a scene reload (models destroyed and rebuilt, commonly at the same addresses the
			// pipeline keyed its resources by). Each stage acts, waits for the frames it triggered
			// to settle, then advances; validation and the sanitizers read the result.
			// Minimizing is X11-only: on Wayland a minimized surface leaves the frame loop parked
			// in DeviceContext::WaitOnWindowResized until the window is restored, and a headless
			// run cannot arrange the restore from inside the loop it is parked in. The headless
			// presets build GLFW's X11 backend, so the gated run takes the stage.
			if (smokeTest)
			{
				++smokeFrames;
				if (smokeStage < 5 && smokeFrames >= 4 && !smokeActionPending)
				{
					smokeActionPending = true;
					smokeFrames = 0;
					if (smokeStage == 0 || smokeStage == 1)
					{
						// Ask for the size relative to the current one, growing then shrinking: the
						// opening size, the compositor's idea of it and the Xvfb screen all vary, and a
						// fixed target can land on the size the window already has, which is not a
						// resize at all.
						int width = 0;
						int height = 0;
						glfwGetWindowSize(pWindow->GetGLFWWindow(), &width, &height);
						const int delta = smokeStage == 0 ? 192 : -128;
						glfwSetWindowSize(
						    pWindow->GetGLFWWindow(), std::max(160, width + delta),
						    std::max(120, height + delta * 3 / 4));
					}
					else if (smokeStage == 2)
					{
						// Nothing reports a resize for a minimize on X11, so the stage clears its own
						// pending flag: the point is that the engine lives through the cycle.
						if (smokeCanIconify)
							glfwIconifyWindow(pWindow->GetGLFWWindow());
						smokeActionPending = false;
					}
					else if (smokeStage == 3)
					{
						if (smokeCanIconify)
							glfwRestoreWindow(pWindow->GetGLFWWindow());
						smokeActionPending = false;
					}
					else if (smokeStage == 4)
					{
						scene.Load(scenePath);
						++smokeReloads;
						smokeActionPending = false;
					}
					++smokeStage;
				}
				if (smokeStage == 5 && smokeFrames >= 4)
					glfwSetWindowShouldClose(pWindow->GetGLFWWindow(), GLFW_TRUE);
				// The whole sequence on a software driver, generously: lavapipe is a few hundred
				// milliseconds a frame at these sizes, and a runner is slower than this machine.
				if (std::chrono::steady_clock::now() - smokeStart > std::chrono::seconds(120))
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
		// The sequence runs on the compositor's terms, so the gate requires one handled resize,
		// not two: a tiling window manager can refuse the second. CI's xvfb-run has no window
		// manager and takes both.
		if (smokeTest && (smokeResizes < 1 || smokeReloads != 1 || smokeStage != 5))
			throw std::runtime_error(
			    "Model rendering smoke test did not complete its sequence (resizes=" +
			    std::to_string(smokeResizes) + ", reloads=" + std::to_string(smokeReloads) +
			    ", stage=" + std::to_string(smokeStage) + ")");
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return EXIT_FAILURE;
	}

	return 0;
}
} // namespace

int RunRenderApplication(
    int argc, char **argv, const char *windowTitle, const std::filesystem::path &assetSubdirectory)
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
} // namespace Jettison::Renderer