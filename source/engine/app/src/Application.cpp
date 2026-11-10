#include "Application.h"

// GLFW / Vulkan.
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <jettison/app/CelestialPresentation.h>
#include <jettison/app/SimPresentation.h>
#include <jettison/profile/Profile.h>
#include <jettison/render/Scene.h>
#include <jettison/render/vulkan/DeviceContext.h>
#include <jettison/render/vulkan/Renderer.h>
#include <jettison/render/vulkan/Swapchain.h>
#include <jettison/render/vulkan/Window.h>
#include <jettison/render/vulkan/imgui/ImGuiRenderer.h>

namespace Jettison::App
{
using Render::check_vk_result;
using Render::DeviceContext;
using Render::ImGuiRenderer;
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

// Each celestial body's orbit about where its parent is now, as the line its next revolution
// traces: on rails, so the body is on its line in every frame, and a body off it is a bug in the
// evaluation or in the placement.
void DrawOrbits(
    Render::DebugDraw &draw, const entt::registry &registry, const Celestial::StarSystem &system,
    double time)
{
	constexpr std::size_t kPoints = 256;
	for (const auto [entity, celestial] : registry.view<const Render::CelestialBodyComponent>().each())
	{
		const int index = system.Find(celestial.body);
		if (index < 0)
			continue;
		const Celestial::Body &body = system.bodies[static_cast<std::size_t>(index)];
		if (body.parentIndex < 0 || body.orbit.semiMajorAxis == 0.0)
			continue;
		const glm::dvec3 parent =
		    Celestial::EvaluateBody(system, static_cast<std::size_t>(body.parentIndex), time).metres;
		std::vector<glm::dvec3> points(kPoints);
		for (std::size_t point = 0; point < kPoints; ++point)
			points[point] =
			    parent + Celestial::Evaluate(
			                 body.orbit, time + body.orbit.period * static_cast<double>(point) /
			                                        static_cast<double>(kPoints));
		draw.Polyline(points, {0.3f, 0.8f, 1.0f, 0.6f}, true);
	}
}
} // namespace

Application::Application(AppArguments arguments, std::string windowTitle)
    : m_arguments{std::move(arguments)}, m_windowTitle{std::move(windowTitle)}
{
	m_toggleUi = m_input.DefineAction("toggle_ui", Input::ActionType::Button);
	m_toggleConsole = m_input.DefineAction("toggle_console", Input::ActionType::Button);
	m_back = m_input.DefineAction("back", Input::ActionType::Button);
	m_toggleDebugDraw = m_input.DefineAction("toggle_debug_draw", Input::ActionType::Button);
	m_cameraOrbit = m_input.DefineAction("camera_orbit", Input::ActionType::Vector);
	m_cameraZoom = m_input.DefineAction("camera_zoom", Input::ActionType::Axis);
	m_cameraDrag = m_input.DefineAction("camera_drag", Input::ActionType::Button);
	m_cameraLook = m_input.DefineAction("camera_look", Input::ActionType::Vector);
	m_cameraNext = m_input.DefineAction("camera_next", Input::ActionType::Button);
	m_input.DefineContext("shell");
	m_input.DefineContext("console", true);
	for (const char *context : {"shell", "console"})
	{
		m_input.Bind(context, m_toggleUi, {Input::Keys::F1});
		m_input.Bind(context, m_toggleConsole, {Input::Keys::GraveAccent});
		m_input.Bind(context, m_back, {Input::Keys::Escape});
	}
	m_input.Bind("shell", m_toggleDebugDraw, {Input::Keys::F2});
	using Input::Keys::Character;
	m_input.Bind("shell", m_cameraOrbit, {Character('d'), Character('a'), 1.0f, 0.0f, 0});
	m_input.Bind("shell", m_cameraOrbit, {Character('w'), Character('s'), 1.0f, 0.0f, 1});
	m_input.Bind("shell", m_cameraZoom, {Character('e'), Character('q')});
	m_input.Bind("shell", m_cameraDrag, {{Input::Device::MouseButton, 1}});
	m_input.Bind("shell", m_cameraLook, {{Input::Device::MouseAxis, 0}, {}, 1.0f, 0.0f, 0});
	m_input.Bind("shell", m_cameraLook, {{Input::Device::MouseAxis, 1}, {}, 1.0f, 0.0f, 1});
	m_input.Bind("shell", m_cameraNext, {Character('c')});
	m_input.PushContext("shell");

	m_console.CaptureLog();
	m_console.RegisterCommand(
	    "quit", "Closes the window and ends the run",
	    [this](DevTools::Console &, std::span<const std::string>) { RequestClose(); });

	Sim::RegisterTypes();
	m_sim.RegisterComponent<Sim::Transform>();
	RegisterFrameSystems();
}


void Application::RegisterFrameSystems()
{
	using Animation::AnimationPlayerComponent;
	using Animation::RotationAnimationComponent;
	using Animation::SkeletonComponent;
	using Ecs::Access;
	using Ecs::TickPhase;
	using Render::CameraComponent;
	using Render::LightComponent;
	using Render::MeshComponent;
	using Render::ParentComponent;
	using Render::TransformComponent;
	using Render::WorldTransformComponent;

	m_frame.Register(
	    {"shell.input",
	     TickPhase::Input,
	     {},
	     {},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("shell.input");
		     m_debugDraw.Clear();
		     glfwPollEvents();
		     PollInput();
	     }});

	// The commands phase is empty in the shell: it has no actions that become commands, and a
	// game's register here.

	m_frame.Register(
	    {"sim.step",
	     TickPhase::Simulation,
	     {},
	     {},
	     [this](entt::registry &, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("sim.step");
		     const int steps = m_simStepper.Advance(deltaTime);
		     JETTISON_PROFILE_PLOT("sim.steps", steps);
	     }});

	m_frame.Register(
	    {"sim.sync",
	     TickPhase::Sync,
	     Access::Of<SimProxyComponent>().Writing<TransformComponent>(),
	     {},
	     [this](entt::registry &registry, double)
	     {
		     JETTISON_PROFILE_ZONE("sim.sync");
		     SyncSimProxies(
		         registry, m_simStepper.Previous(), m_simStepper.Current(), m_simStepper.Alpha());
	     }});
	// Nothing in the shell turns an event into sound or UI yet. Taking them every frame is what
	// keeps the queue from growing while nothing does; a game's handlers replace this system.
	// Bodies on rails, at the moment presentation shows: between the last two steps, in time.
	m_frame.Register(
	    {"celestial.sync",
	     TickPhase::Sync,
	     Access::Of<Render::CelestialBodyComponent>().Writing<Render::PlacementComponent>(),
	     {"sim.sync"},
	     [this](entt::registry &registry, double)
	     {
		     if (!m_starSystem)
			     return;
		     JETTISON_PROFILE_ZONE("celestial.sync");
		     SyncCelestialBodies(
		         registry, *m_starSystem,
		         Celestial::PresentationTime(
		             m_simStepper.Previous().time, m_simStepper.Current().time, m_simStepper.Alpha()));
	     }});
	m_frame.Register(
	    {"sim.events",
	     TickPhase::Sync,
	     {},
	     {},
	     [this](entt::registry &, double) { static_cast<void>(m_sim.Events().Take()); }});

	m_frame.Register(
	    {"scene.update",
	     TickPhase::Presentation,
	     Access::Of<TransformComponent, ParentComponent, MeshComponent>()
	         .Writing<
	             RotationAnimationComponent, AnimationPlayerComponent, SkeletonComponent,
	             WorldTransformComponent>(),
	     {},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("scene.update");
		     m_sceneUpdateSystem.Update(registry, static_cast<float>(deltaTime));
	     }});
	// After the world transforms, which the rigs follow, and before anything that reads the pose.
	m_frame.Register(
	    {"camera.rig",
	     TickPhase::Presentation,
	     Access::Of<WorldTransformComponent>()
	         .Writing<Render::OrbitCameraComponent, Render::CameraPoseComponent>(),
	     {"scene.update"},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("camera.rig");
		     DriveCameras(registry, deltaTime);
		     Render::UpdateOrbitCameras(registry, deltaTime);
	     }});
	m_frame.Register(
	    {"scene.debug",
	     TickPhase::Presentation,
	     Access::Of<WorldTransformComponent, MeshComponent, LightComponent>(),
	     {"scene.update"},
	     [this](entt::registry &registry, double)
	     {
		     if (!m_showSceneDebug)
			     return;
		     for (const auto [entity, world] : registry.view<const WorldTransformComponent>().each())
		     {
			     const glm::dvec3 origin{world.matrix[3]};
			     // The rotation without the scale: a mesh scaled twelve times would otherwise
			     // draw axes twelve times as long.
			     const glm::dmat3 basis{
			         glm::normalize(glm::dvec3{world.matrix[0]}),
			         glm::normalize(glm::dvec3{world.matrix[1]}),
			         glm::normalize(glm::dvec3{world.matrix[2]})};
			     if (registry.all_of<MeshComponent>(entity))
				     m_debugDraw.Axes(origin, glm::quat_cast(basis), 0.3, Render::DebugDepth::Overlay);
			     if (registry.all_of<LightComponent>(entity))
				     m_debugDraw.Sphere(origin, 0.15, {1.0f, 0.9f, 0.4f, 1.0f});
		     }
		     if (m_starSystem)
			     DrawOrbits(
			         m_debugDraw, registry, *m_starSystem,
			         Celestial::PresentationTime(
			             m_simStepper.Previous().time, m_simStepper.Current().time,
			             m_simStepper.Alpha()));
	     }});
	m_frame.Register(
	    {"shell.ui",
	     TickPhase::Presentation,
	     {},
	     {},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("shell.ui");
		     DrawUI();
	     }});

	m_frame.Register(
	    {"render",
	     TickPhase::Render,
	     Access::Of<
	         MeshComponent, WorldTransformComponent, SkeletonComponent, CameraComponent,
	         LightComponent>(),
	     {},
	     [this](entt::registry &, double)
	     {
		     // The answer says whether this frame recreated the swapchain, which is the only sign
		     // of a handled resize the headless sequence can read: it has no way to see a window
		     // that resized without the engine noticing, and a resize the engine did not recreate
		     // a swapchain for is not one it handled.
		     m_frameResized =
		         m_pRenderer->Render(*m_scene, m_sceneRenderSystem, m_debugDraw, *m_pImGuiRenderer);
	     }});
	// After the render, so the frame it judges is the one just submitted; its reload stage
	// replaces the scene, which nothing after it in the frame reads.
	m_frame.Register(
	    {"shell.smoke",
	     TickPhase::Render,
	     {},
	     {"render"},
	     [this](entt::registry &, double)
	     {
		     if (!m_smokeSequence)
			     return;
		     int width = 0;
		     int height = 0;
		     glfwGetWindowSize(m_pWindow->GetGLFWWindow(), &width, &height);
		     m_smokeSequence->Advance(m_frameResized, width, height, m_frameStart);
	     }});

	m_frame.Validate();
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
	InitRenderer();
	InitUI();
	// The headless run renders the dev tools, the demo window and the console: the UI path is part of
	// what the render test exercises, and an interactive run starts without it and asks for it with F1.
	m_showUI = m_arguments.smokeTest;
	m_devTools.showDemo = m_arguments.smokeTest;
	m_devTools.showConsole = m_arguments.smokeTest;
	m_showSceneDebug = m_arguments.smokeTest;
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
	LoadScene();
	if (!m_arguments.saveScenePath.empty())
		m_scene->Save(m_arguments.saveScenePath);
}


void Application::InitRenderer()
{
	if (m_assets.Mounts().empty())
		m_assets.AddMount({"base", m_arguments.assetDirectory});
	m_pRenderer = std::make_shared<Renderer>(m_pDeviceContext, m_pWindow, m_pSwapchain, m_assets);
	m_pRenderer->Init();
}


void Application::InitUI()
{
	m_pImGuiRenderer = std::make_unique<ImGuiRenderer>(m_pDeviceContext, m_arguments.assetDirectory);
	m_pImGuiRenderer->Init(
	    m_pWindow->GetGLFWWindow(), m_pRenderer->GetOutputFormat(), Renderer::kFramesInFlight);
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
	m_frameStart = Time::RealClock::Clock::now();
	const double deltaTime = m_frameClock.Tick(m_frameStart).count();
	for (const Ecs::TickPhase phase : Ecs::kTickPhases)
		m_frame.Run(phase, m_scene->Registry(), deltaTime);
	JETTISON_PROFILE_FRAME();
}


void Application::DrawUI()
{
	m_pImGuiRenderer->BeginFrame();
	if (m_showUI)
		DevTools::DrawDevTools(m_console, m_devTools, &m_showUI);
	else if (m_devTools.showConsole)
		DevTools::DrawConsole(m_console, m_devTools.console, &m_devTools.showConsole);
	m_pImGuiRenderer->EndFrame();
}


void Application::PollInput()
{
	m_glfwInput.Feed(m_pWindow->GetGLFWWindow(), m_input);
	// The console may have been closed by its window's own button last frame.
	SyncConsoleContext();
	m_input.Poll();

	// On the press edge, not the level: a held key is one action, not every frame's action, and
	// the frame loop runs faster than a hand.
	if (m_input.WasPressedThisFrame(m_toggleUi))
		m_showUI = !m_showUI;
	if (m_input.WasPressedThisFrame(m_toggleDebugDraw))
		m_showSceneDebug = !m_showSceneDebug;
	if (m_input.WasPressedThisFrame(m_cameraNext))
		NextCameraTarget(m_scene->Registry());
	if (m_input.WasPressedThisFrame(m_toggleConsole))
	{
		m_devTools.showConsole = !m_devTools.showConsole;
		m_devTools.console.focusInput = m_devTools.showConsole;
	}

	// Escape closes what is open before it closes the window, so the key that dismisses a panel
	// is not the key that ends the run.
	if (m_input.WasPressedThisFrame(m_back))
	{
		if (m_devTools.showConsole)
			m_devTools.showConsole = false;
		else if (m_showUI)
			m_showUI = false;
		else
			RequestClose();
	}
	SyncConsoleContext();
}


void Application::LoadScene()
{
	m_scene->Load(m_arguments.scenePath);
	m_starSystem.reset();
	if (const auto &path = m_scene->StarSystemPath(); !path.empty())
	{
		std::ifstream file{path};
		if (!file)
			throw std::runtime_error("Failed to open the scene's star system: " + path.string());
		std::stringstream text;
		text << file.rdbuf();
		m_starSystem = Celestial::ReadStarSystem(text.str());
		ResolveCelestialBodies(m_scene->Registry(), *m_starSystem);
	}
	m_simStepper.Resync();
}


void Application::DriveCameras(entt::registry &registry, double deltaTime)
{
	// Radians a second for the keys, radians a pixel for a drag, and the share of the distance
	// a second of zoom covers.
	constexpr double kTurnRate = 1.2;
	constexpr double kDragRate = 0.005;
	constexpr double kZoomRate = 1.5;
	const glm::vec2 keys = m_input.Vector(m_cameraOrbit);
	glm::dvec2 turn = glm::dvec2{keys} * kTurnRate * deltaTime;
	if (m_input.IsPressed(m_cameraDrag))
		turn -= glm::dvec2{m_input.Vector(m_cameraLook)} * kDragRate;
	const double zoom = std::exp(-double{m_input.Axis(m_cameraZoom)} * kZoomRate * deltaTime);
	for (const auto [entity, orbit] : registry.view<Render::OrbitCameraComponent>().each())
	{
		orbit.rig.Orbit(turn.x, turn.y);
		orbit.rig.Zoom(zoom);
	}
}


void Application::NextCameraTarget(entt::registry &registry)
{
	std::vector<entt::entity> bodies;
	for (const entt::entity entity : registry.view<const Render::CelestialBodyComponent>())
		bodies.push_back(entity);
	if (bodies.empty())
		return;
	std::ranges::sort(bodies);
	for (const auto [entity, orbit] : registry.view<Render::OrbitCameraComponent>().each())
	{
		const auto current = std::ranges::find(bodies, orbit.target);
		const entt::entity next = current == bodies.end() || std::next(current) == bodies.end()
		                              ? bodies.front()
		                              : *std::next(current);
		// Out to three of the body's radii, eased over two seconds from wherever the camera is.
		double radius = 50.0;
		if (m_starSystem)
			if (const int index =
			        m_starSystem->Find(registry.get<Render::CelestialBodyComponent>(next).body);
			    index >= 0)
				radius = std::max(radius, m_starSystem->bodies[static_cast<std::size_t>(index)].radius);
		if (const auto *pose = registry.try_get<Render::CameraPoseComponent>(entity))
			orbit.transition = Render::CameraTransition{
			    {pose->position, pose->orientation}, 2.0, Math::Easing::InOutCubic};
		orbit.target = next;
		orbit.minDistance = radius * 1.1;
		orbit.distance = radius * 3.0;
		orbit.placed = false;
	}
}


void Application::SyncConsoleContext()
{
	if (m_devTools.showConsole == m_consoleContext)
		return;
	if (m_devTools.showConsole)
		m_input.PushContext("console");
	else
		m_input.PopContext();
	m_consoleContext = m_devTools.showConsole;
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
	actions.reload = [this] { LoadScene(); };
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
	m_scene.reset();
	m_smokeSequence.reset();
	m_pSwapchain.reset();
	m_pDeviceContext.reset();
	m_pWindow.reset();

	m_state = State::Stopped;
}
} // namespace Jettison::App
