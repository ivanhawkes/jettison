#include "Application.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include <jettison/profile/Profile.h>
#include <jettison/render/vulkan/DeviceContext.h>
#include <jettison/render/vulkan/Pipeline.h>
#include <jettison/render/vulkan/Renderer.h>
#include <jettison/render/vulkan/Swapchain.h>
#include <jettison/render/vulkan/Window.h>
#include <jettison/render/vulkan/imgui/ImGuiRenderer.h>

namespace Jettison::App
{
using Render::check_vk_result;
using Render::DeviceContext;
using Render::ImGuiRenderer;
using Render::Pipeline;
using Render::Renderer;
using Render::Scene;
using Render::Swapchain;
using Render::Window;

namespace
{
// Software rendering is fill-rate bound, and the window opens at 1920x1080. The headless sequence
// is six stages of a handful of frames, so it starts on a small window: every frame after this
// costs a quarter of what the opening size costs, and the resize stages still change the size.
constexpr int kSmokeWindowWidth = 640;
constexpr int kSmokeWindowHeight = 480;

// Minimizing is X11-only: on Wayland a minimized surface leaves the frame loop parked in
// DeviceContext::WaitOnWindowResized until the window is restored, and a headless run cannot
// arrange the restore from inside the loop it is parked in. The headless presets build GLFW's X11
// backend, so the gated run takes the stage.
bool CanIconifyWindow()
{
	return glfwGetPlatform() == GLFW_PLATFORM_X11;
}
} // namespace

Application::Application(AppArguments arguments, std::string windowTitle)
    : m_arguments{std::move(arguments)}, m_windowTitle{std::move(windowTitle)}
{
	m_toggleUi = m_input.DefineAction("toggle_ui", Input::ActionType::Button);
	m_back = m_input.DefineAction("back", Input::ActionType::Button);
	m_input.DefineContext("shell");
	m_input.Bind("shell", m_toggleUi, {Input::Keys::F1});
	m_input.Bind("shell", m_back, {Input::Keys::Escape});
	m_input.PushContext("shell");
}


Application::~Application()
{
	// The lifecycle's last word: a shell that is going out of scope has to have released
	// everything it made, on the path that returns from Run() and on the one that throws out of
	// it. Shutdown() is noexcept and idempotent, so a loop that already tore down costs nothing.
	Shutdown();
}


void Application::Run()
{
	Init();
	RunFrameLoop();
	// The verdict is read after teardown: the counters are the sequence's own, so nothing of the
	// engine has to stay alive to read them, and a run that fails the gate is a run that should
	// report on a device it has already released.
	if (m_smokeSequence && !m_smokeSequence->Passed())
		throw std::runtime_error(
		    "Model rendering smoke test did not complete its sequence (" + m_smokeSequence->Summary() +
		    ")");
}


void Application::Init()
{
	m_state = State::Initialising;
	InitWindow();
	InitVulkan();
	InitScene();
	InitPipeline();
	InitUI();
	InitRenderer();
	// The headless run renders the demo window: the UI path is part of what the render test
	// exercises, and an interactive run starts without it and asks for it with F1.
	m_showUI = m_arguments.smokeTest;
	m_state = State::Ready;
}


void Application::InitWindow()
{
	m_pWindow = std::make_shared<Window>();
	m_pWindow->Init();
	glfwSetWindowTitle(m_pWindow->GetGLFWWindow(), m_windowTitle.c_str());
}


void Application::InitVulkan()
{
	m_pDeviceContext = std::make_shared<DeviceContext>(m_pWindow);
	m_pDeviceContext->Init();
#ifndef NDEBUG
	if (m_arguments.smokeTest && !m_pDeviceContext->DebugUtilsActive())
		throw std::runtime_error("Vulkan debug messenger is required for the smoke test");
#endif
	m_pSwapchain = std::make_shared<Swapchain>(m_pDeviceContext);
	m_pSwapchain->Init();
}


void Application::InitScene()
{
	m_scene = std::make_unique<Scene>(m_pDeviceContext);
	m_scene->Load(m_arguments.scenePath);
	if (!m_arguments.saveScenePath.empty())
		m_scene->Save(m_arguments.saveScenePath);
}


void Application::InitPipeline()
{
	m_pPipeline =
	    std::make_shared<Pipeline>(m_pDeviceContext, m_pSwapchain, m_arguments.assetDirectory);
	m_pPipeline->Init();
	m_pPipeline->CreateCommandBuffers();
}


void Application::InitUI()
{
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(m_pDeviceContext, m_arguments.assetDirectory);
	m_pImGuiRenderer->Init(
	    m_pWindow->GetGLFWWindow(), m_pPipeline->GetRenderPass(), m_pSwapchain->GetImageCount());
}


void Application::InitRenderer()
{
	m_pRenderer = std::make_shared<Renderer>(m_pDeviceContext, m_pWindow, m_pSwapchain, m_pPipeline);
	m_pRenderer->Init();
	if (m_arguments.smokeTest)
		glfwSetWindowSize(m_pWindow->GetGLFWWindow(), kSmokeWindowWidth, kSmokeWindowHeight);
}


void Application::RunFrameLoop()
{
	m_state = State::Running;
	// The sequence's frames, and its watchdog, start here: the window is on screen, the scene is
	// loaded, and the frames it counts are the frames of the run it is testing.
	if (m_arguments.smokeTest)
		m_smokeSequence = std::make_unique<SmokeSequence>(MakeSmokeActions(), CanIconifyWindow());
	m_frameClock.Tick();

	while (!glfwWindowShouldClose(m_pWindow->GetGLFWWindow()))
		Tick();

	Shutdown();
}


void Application::Tick()
{
	JETTISON_PROFILE_FUNCTION();
	const auto now = Time::RealClock::Clock::now();
	const auto deltaTime = static_cast<float>(m_frameClock.Tick(now).count());

	glfwPollEvents();
	PollInput();

	m_pImGuiRenderer->BeginFrame();
	if (m_showUI)
		m_pImGuiRenderer->ShowDemoWindow(&m_showUI);
	m_pImGuiRenderer->EndFrame();

	// The answer says whether this frame recreated the swapchain, which is the only sign of a
	// handled resize the headless sequence can read: it has no way to see a window that resized
	// without the engine noticing, and a resize the engine did not recreate a swapchain for is not
	// one it handled.
	const bool frameResized = m_pRenderer->Render(
	    *m_scene, m_sceneUpdateSystem, m_sceneRenderSystem, deltaTime, *m_pImGuiRenderer);

	if (m_smokeSequence)
	{
		int width = 0;
		int height = 0;
		glfwGetWindowSize(m_pWindow->GetGLFWWindow(), &width, &height);
		m_smokeSequence->Advance(frameResized, width, height, now);
	}
	JETTISON_PROFILE_FRAME();
}


void Application::PollInput()
{
	m_glfwInput.Feed(m_pWindow->GetGLFWWindow(), m_input);
	m_input.Poll();

	// On the press edge, not the level: a held key is one action, not every frame's action, and
	// the frame loop runs faster than a hand.
	if (m_input.WasPressedThisFrame(m_toggleUi))
		m_showUI = !m_showUI;

	// Escape closes the UI before it closes the window, so the key that dismisses a panel is not
	// the key that ends the run.
	if (m_input.WasPressedThisFrame(m_back))
	{
		if (m_showUI)
			m_showUI = false;
		else
			RequestClose();
	}
}


void Application::RequestClose()
{
	glfwSetWindowShouldClose(m_pWindow->GetGLFWWindow(), GLFW_TRUE);
}


SmokeActions Application::MakeSmokeActions()
{
	SmokeActions actions;
	actions.resize = [this](int width, int height)
	{ glfwSetWindowSize(m_pWindow->GetGLFWWindow(), width, height); };
	actions.iconify = [this] { glfwIconifyWindow(m_pWindow->GetGLFWWindow()); };
	actions.restore = [this] { glfwRestoreWindow(m_pWindow->GetGLFWWindow()); };
	// The reload is the whole Load() path: Scene::Destroy, then the models, textures and buffers
	// built again, at the addresses the pipeline has cached resources under.
	actions.reload = [this] { m_scene->Load(m_arguments.scenePath); };
	actions.close = [this] { RequestClose(); };
	return actions;
}


void Application::Shutdown() noexcept
{
	if (m_state == State::Stopped)
		return;
	m_state = State::Stopping;

	// Teardown in the reverse order of Init(), which is not decoration: the order is the only
	// thing between a submitted command buffer and the VUID that reports destroying the buffer it
	// references, and the headless sequence walks that path on purpose.
	try
	{
		// Everything the frame loop submitted has to have landed before the objects it references
		// go away. The loop's last frame is submitted, not waited on: Renderer::Render returns
		// once the work is in the queue, so this wait is the frame loop's last fence.
		if (m_pDeviceContext)
			check_vk_result(m_pDeviceContext->WaitIdle());

		if (m_pImGuiRenderer)
			m_pImGuiRenderer->Destroy();
		if (m_pRenderer)
			m_pRenderer->Destroy();
		if (m_pPipeline)
			m_pPipeline->Destroy();
		if (m_scene)
			m_scene->Destroy();
		if (m_pSwapchain)
			m_pSwapchain->Destroy();
		if (m_pDeviceContext)
			m_pDeviceContext->Destroy();
		if (m_pWindow)
			m_pWindow->Destroy();
	}
	catch (const std::exception &e)
	{
		// The destructor reaches here with an exception in flight, so nothing may leave this
		// function. The report that matters is the one the entry point prints about the exception
		// that unwound here, and an object this path could not release is a leak in a process the
		// operating system is already reclaiming.
		std::cerr << "Teardown: " << e.what() << '\n';
	}
	catch (...)
	{
		std::cerr << "Teardown: unknown error\n";
	}

	// The references go together, after the calls: a shared_ptr reaching zero in an arbitrary order
	// cannot be the thing that releases an object, because nothing in this tree releases anything
	// in a destructor. Every Destroy() above has run by here, and the order of the resets carries
	// nothing.
	m_pImGuiRenderer.reset();
	m_pRenderer.reset();
	m_pPipeline.reset();
	m_scene.reset();
	m_smokeSequence.reset();
	m_pSwapchain.reset();
	m_pDeviceContext.reset();
	m_pWindow.reset();

	m_state = State::Stopped;
}
} // namespace Jettison::App
