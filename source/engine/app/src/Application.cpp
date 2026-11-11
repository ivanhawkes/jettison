#include "Application.h"

// The window's input and size; the renderer behind IRenderer owns the window itself.
#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <jettison/animation/Components.h>
#include <jettison/animation/RigLoader.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/profile/Profile.h>
#include <jettison/render/Camera.h>
#include <jettison/render/IRenderer.h>
#include <jettison/render/SceneView.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/scene/SystemScene.h>
#include <jettison/sim/SaveGame.h>

#include "Docking.h"
#include "Saves.h"

namespace Jettison::App
{

namespace
{
// Software rendering is fill-rate bound, and the window opens at 1920x1080. The headless sequence
// is seven stages of a handful of frames, so it starts on a small window: every frame after this
// costs a quarter of what the opening size costs, and the resize stages still change the size.
constexpr int kSmokeWindowWidth = 640;
constexpr int kSmokeWindowHeight = 480;
// Frames a benchmark draws before it times any: the pipelines a material builds on its first draw,
// the textures' uploads, and the driver's own warming up are not the frame it is measuring.
constexpr std::uint32_t kBenchmarkWarmUp = 60;
// What a jump's effect covers the frame with: black, so the frames it holds while the new system
// loads are no brighter than space.
constexpr glm::vec3 kJumpColor{0.0f};
constexpr Core::Category kShell{"shell"};

// The shell's ship: a shuttle about ten metres long, as its proxy draws it, whose collision box is
// the proxy's bounds. Held at 150 m off the station's dock, nose towards it.
constexpr std::string_view kShipShape = "ships/shuttle";
constexpr glm::vec3 kShipHalfExtents{3.2f, 0.9f, 4.9f};
constexpr float kShipMass = 20000.0f;
constexpr glm::dvec3 kShipStation{0.0, 0.0, -150.0};
// The docking port: a box 40 m on a side before the dock at the hull's -Z end, reaching 2 m into
// the hull past the dock's face, so a ship held against the dock is in it.
constexpr glm::vec3 kPortHalfExtents{20.0f, 20.0f, 20.0f};
constexpr double kPortOverlap = 2.0;
// How far a pick reaches: past any station a ship is flown by.
constexpr double kPickDistance = 50000.0;
// How long the camera eases into and out of the ship's rigs, and between them.
constexpr double kPilotTransition = 1.5;
constexpr double kViewTransition = 0.5;
// In the smoke run nothing presses a key: the ship flies ahead at a fifth of its speed.
constexpr Physics::FlightControls kSmokeFlight{{0.0f, 0.0f, -0.2f}, {0.0f, 0.05f, 0.1f}};

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
	for (const auto [entity, celestial] :
	     registry.view<const Scene::CelestialBodyComponent>(entt::exclude<Scene::HiddenComponent>)
	         .each())
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
// A light's reach: a point light's range as a sphere, and a spot's cone out to its range, the
// outer angle's edge drawn as the circle where the cone meets the range's sphere.
void DrawLight(
    Render::DebugDraw &draw, const Scene::LightComponent &light, const glm::dvec3 &origin,
    const glm::dvec3 &direction)
{
	const glm::vec4 color{light.color, 0.6f};
	draw.Sphere(origin, 0.15, {1.0f, 0.9f, 0.4f, 1.0f});
	if (light.type != Scene::LightType::Spot)
	{
		draw.Sphere(origin, light.range, color);
		return;
	}
	const double along = light.range * std::cos(double{light.outerConeAngle});
	const double across = light.range * std::sin(double{light.outerConeAngle});
	const glm::dvec3 center = origin + direction * along;
	draw.Circle(center, direction, across, color, 32);
	const glm::dvec3 helper =
	    std::abs(direction.y) < 0.9 ? glm::dvec3{0.0, 1.0, 0.0} : glm::dvec3{1.0, 0.0, 0.0};
	const glm::dvec3 side = glm::normalize(glm::cross(direction, helper));
	const glm::dvec3 up = glm::cross(direction, side);
	for (const glm::dvec3 &edge : {side, -side, up, -up})
		draw.Line(origin, center + edge * across, color);
}
} // namespace

Application::Application(AppArguments arguments, std::string windowTitle)
    : m_arguments{std::move(arguments)}, m_windowTitle{std::move(windowTitle)}
{
	m_toggleUi = m_input.DefineAction("toggle_ui", Input::ActionType::Button);
	m_toggleConsole = m_input.DefineAction("toggle_console", Input::ActionType::Button);
	m_back = m_input.DefineAction("back", Input::ActionType::Button);
	m_toggleDebugDraw = m_input.DefineAction("toggle_debug_draw", Input::ActionType::Button);
	m_toggleClusterHeat = m_input.DefineAction("toggle_cluster_heat", Input::ActionType::Button);
	m_toggleShadowCascades = m_input.DefineAction("toggle_shadow_cascades", Input::ActionType::Button);
	m_cameraOrbit = m_input.DefineAction("camera_orbit", Input::ActionType::Vector);
	m_cameraZoom = m_input.DefineAction("camera_zoom", Input::ActionType::Axis);
	m_cameraDrag = m_input.DefineAction("camera_drag", Input::ActionType::Button);
	m_cameraLook = m_input.DefineAction("camera_look", Input::ActionType::Vector);
	m_cameraNext = m_input.DefineAction("camera_next", Input::ActionType::Button);
	m_cameraWheel = m_input.DefineAction("camera_wheel", Input::ActionType::Vector);
	m_jumpNext = m_input.DefineAction("jump_next", Input::ActionType::Button);
	m_pilot = m_input.DefineAction("pilot", Input::ActionType::Button);
	m_pickAction = m_input.DefineAction("pick", Input::ActionType::Button);
	m_quickSave = m_input.DefineAction("quicksave", Input::ActionType::Button);
	m_quickLoad = m_input.DefineAction("quickload", Input::ActionType::Button);
	m_input.DefineContext("shell");
	m_input.DefineContext("console", true);
	for (const char *context : {"shell", "console"})
	{
		m_input.Bind(context, m_toggleUi, {Input::Keys::F1});
		m_input.Bind(context, m_toggleConsole, {Input::Keys::GraveAccent});
		m_input.Bind(context, m_back, {Input::Keys::Escape});
	}
	m_input.Bind("shell", m_toggleDebugDraw, {Input::Keys::F2});
	m_input.Bind("shell", m_toggleClusterHeat, {Input::Keys::F3});
	m_input.Bind("shell", m_toggleShadowCascades, {Input::Keys::F4});
	using Input::Keys::Character;
	m_input.Bind("shell", m_cameraOrbit, {Character('d'), Character('a'), 1.0f, 0.0f, 0});
	m_input.Bind("shell", m_cameraOrbit, {Character('w'), Character('s'), 1.0f, 0.0f, 1});
	m_input.Bind("shell", m_cameraZoom, {Character('e'), Character('q')});
	m_input.Bind("shell", m_cameraDrag, {{Input::Device::MouseButton, 1}});
	m_input.Bind("shell", m_cameraLook, {{Input::Device::MouseAxis, 0}, {}, 1.0f, 0.0f, 0});
	m_input.Bind("shell", m_cameraLook, {{Input::Device::MouseAxis, 1}, {}, 1.0f, 0.0f, 1});
	m_input.Bind("shell", m_cameraNext, {Character('c')});
	m_input.Bind("shell", m_jumpNext, {Character('j')});
	m_input.Bind("shell", m_cameraWheel, {{Input::Device::MouseAxis, 3}});
	m_input.Bind("shell", m_pilot, {Character('p')});
	m_input.Bind("shell", m_pilot, {{Input::Device::GamepadButton, 7}});
	m_input.Bind("shell", m_pickAction, {{Input::Device::MouseButton, 0}});
	m_input.Bind("shell", m_quickSave, {Input::Keys::F5});
	m_input.Bind("shell", m_quickLoad, {Input::Keys::F9});
	m_pilotInput.emplace(m_input);
	m_input.PushContext("shell");

	m_console.CaptureLog();
	m_console.RegisterCommand(
	    "quit", "Closes the window and ends the run",
	    [this](DevTools::Console &, std::span<const std::string>) { RequestClose(); });
	m_console.RegisterCommand(
	    "jump", "Jumps to the star system named, or out through the next lane: jump [system]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::string name;
		    for (const std::string &word : arguments)
			    name += (name.empty() ? "" : " ") + word;
		    Jump(name);
	    });

	m_console.RegisterCommand(
	    "bind",
	    "Binds an action, or says what it is bound to, and saves the settings: bind <context> <action> "
	    "[bindings...], as Key.W/Key.S or y:GamepadAxis.LeftY*-1~0.15",
	    [this](DevTools::Console &console, std::span<const std::string> arguments)
	    {
		    const BindResult result = Bind(m_input, arguments);
		    if (result.changed)
			    SaveSettings();
		    console.Print(result.message);
	    });
	m_console.RegisterCommand(
	    "unbind", "Puts an action back to its default bindings: unbind <context> <action>",
	    [this](DevTools::Console &console, std::span<const std::string> arguments)
	    {
		    const BindResult result = Unbind(m_input, arguments);
		    SaveSettings();
		    console.Print(result.message);
	    });
	m_console.RegisterCommand(
	    "save", "Saves the game: save [name], quicksave when none is given",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { SaveTo(arguments.empty() ? "quicksave" : arguments[0]); });
	m_console.RegisterCommand(
	    "load", "Loads a saved game: load [name], quicksave when none is given",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { LoadFrom(arguments.empty() ? "quicksave" : arguments[0]); });
	m_console.RegisterCommand(
	    "saves", "Lists the saved games",
	    [this](DevTools::Console &console, std::span<const std::string>)
	    {
		    for (const std::string &name : ListSaves(m_assets))
		    {
			    const auto bytes = m_assets.Read(SavePath(name));
			    try
			    {
				    const Sim::SaveInfo info = Sim::SaveGame::ReadInfo(*bytes);
				    console.Print(
				        std::format(
				            "{}: step {} in {}, by engine {}", name, info.step,
				            info.system.empty() ? "no system" : info.system, info.engineVersion));
			    }
			    catch (const std::exception &error)
			    {
				    console.PrintError(std::format("{}: {}", name, error.what()));
			    }
		    }
	    });

	Sim::RegisterTypes();
	m_sim.RegisterComponent<Sim::Transform>();
	m_sim.RegisterComponent<Sim::InSystem>();
	m_pPhysics = &Physics::PhysicsWorld::Attach(m_sim);
	RegisterDocking(m_sim, *m_pPhysics);
	RegisterFrameSystems();
}


void Application::RegisterFrameSystems()
{
	using Animation::AnimationPlayerComponent;
	using Animation::RotationAnimationComponent;
	using Animation::SkeletonComponent;
	using Ecs::Access;
	using Ecs::TickPhase;
	using Scene::CameraComponent;
	using Scene::LightComponent;
	using Scene::MeshComponent;
	using Scene::ParentComponent;
	using Scene::RigComponent;
	using Scene::TransformComponent;
	using Scene::WorldTransformComponent;

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

	// The pilot's controls and the mouse's picks, as the commands the next step applies; a game's
	// commands go here too.
	m_frame.Register(
	    {"shell.flight",
	     TickPhase::Commands,
	     {},
	     {},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("shell.flight");
		     FlyShip();
		     if (m_clicked)
			     Pick(*m_clicked);
		     m_clicked.reset();
	     }});

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

	// A jump's stages, first in the sync phase: on the frame its effect covers the screen it replaces
	// the scene, which the syncs after it then place and fill as they do any other.
	m_frame.Register(
	    {"scene.jump",
	     TickPhase::Sync,
	     {},
	     {},
	     [this](entt::registry &, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("scene.jump");
		     m_jump->Advance(deltaTime);
		     m_pRenderer->SetFade(kJumpColor, m_jump->Fade());
		     const bool active = m_jump->Active();
		     if (m_jumpWasActive && !active && m_jump->Error().empty())
			     Core::Log(
			         Core::Severity::Info, kShell, "The simulation ran {} steps through the jump",
			         m_sim.Clock().Step() - m_jumpStep);
		     m_jumpWasActive = active;
	     }});

	m_frame.Register(
	    {"sim.sync",
	     TickPhase::Sync,
	     Access::Of<Scene::SimProxyComponent>().Writing<TransformComponent>(),
	     {"scene.jump"},
	     [this](entt::registry &registry, double)
	     {
		     JETTISON_PROFILE_ZONE("sim.sync");
		     // A system's scene is the traffic layer's, with proxies made and unmade; a scene of no
		     // system only shows the proxies it has.
		     if (m_systemScene)
			     m_systemScene->SyncTraffic(
			         m_simStepper.Previous(), m_simStepper.Current(), m_simStepper.Alpha());
		     else
			     Scene::SyncSimProxies(
			         registry, m_simStepper.Previous(), m_simStepper.Current(), m_simStepper.Alpha());
	     }});
	// Bodies on rails, at the moment presentation shows: between the last two steps, in time.
	m_frame.Register(
	    {"celestial.sync",
	     TickPhase::Sync,
	     Access::Of<Scene::CelestialBodyComponent>().Writing<Scene::PlacementComponent>(),
	     {"sim.sync"},
	     [this](entt::registry &, double)
	     {
		     m_shownTime = Celestial::PresentationTime(
		         m_simStepper.Previous().time, m_simStepper.Current().time, m_simStepper.Alpha());
		     if (!m_systemScene)
			     return;
		     JETTISON_PROFILE_ZONE("celestial.sync");
		     m_systemScene->Place(m_shownTime);
	     }});
	// Every frame, so the queue does not grow with events nothing turns into sound or UI; a game's
	// handlers replace this system.
	m_frame.Register(
	    {"sim.events", TickPhase::Sync, {}, {}, [this](entt::registry &, double) { TakeEvents(); }});

	m_frame.Register(
	    {"scene.update",
	     TickPhase::Presentation,
	     Access::Of<TransformComponent, ParentComponent, MeshComponent>()
	         .Writing<
	             RotationAnimationComponent, AnimationPlayerComponent, RigComponent, SkeletonComponent,
	             WorldTransformComponent>(),
	     {},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("scene.update");
		     m_sceneUpdateSystem->Update(registry, static_cast<float>(deltaTime));
	     }});
	m_frame.Register(
	    {"shell.pilot",
	     TickPhase::Presentation,
	     Access::Of<Scene::SimProxyComponent, CameraComponent>().Writing<Scene::PilotCameraComponent>(),
	     {"scene.update"},
	     [this](entt::registry &registry, double) { SyncPilotCamera(registry); }});
	// After the world transforms, which the rigs follow, and before anything that reads the pose.
	m_frame.Register(
	    {"camera.rig",
	     TickPhase::Presentation,
	     Access::Of<WorldTransformComponent>()
	         .Writing<
	             Scene::OrbitCameraComponent, Scene::PilotCameraComponent,
	             Scene::CameraPoseComponent>(),
	     {"scene.update", "shell.pilot"},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("camera.rig");
		     DriveCameras(registry, deltaTime);
		     Scene::UpdateOrbitCameras(registry, deltaTime);
		     Scene::UpdatePilotCameras(registry, deltaTime);
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
		     for (const auto [entity, world] :
		          registry.view<const WorldTransformComponent>(entt::exclude<Scene::HiddenComponent>)
		              .each())
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
			     if (const auto *light = registry.try_get<LightComponent>(entity))
				     DrawLight(m_debugDraw, *light, origin, -basis[2]);
		     }
		     if (m_systemScene)
			     DrawOrbits(
			         m_debugDraw, registry, m_systemScene->System(),
			         Celestial::PresentationTime(
			             m_simStepper.Previous().time, m_simStepper.Current().time,
			             m_simStepper.Alpha()));
	     }});
	// Before the render, so a texture or material that finished loading, or reloaded, draws this
	// frame.
	m_frame.Register(
	    {"resource.update",
	     TickPhase::Presentation,
	     {},
	     {},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("resource.update");
		     m_pResources->Update();
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
		     const double presentationTime =
		         std::chrono::duration<double>(m_frameStart - m_loopStart).count();
		     m_frameResized = m_pRenderer->Render(*m_pScene, m_debugDraw, presentationTime);
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
		     glfwGetWindowSize(m_pRenderer->GetGLFWWindow(), &width, &height);
		     m_smokeSequence->Advance(m_frameResized, width, height, m_frameStart);
	     }});
	m_frame.Register(
	    {"shell.benchmark",
	     TickPhase::Render,
	     {},
	     {"render"},
	     [this](entt::registry &, double)
	     {
		     if (m_arguments.benchmarkFrames != 0)
			     AdvanceBenchmark();
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
	// Every frame's light assignment was read back and compared with the CPU's. A run in which no
	// cluster listed a light proved nothing about the assignment, so it fails as well.
	if (m_arguments.benchmarkFrames != 0)
		ReportBenchmark();
	if (m_smokeSequence && !m_passTimings.empty())
	{
		// What the run's frames cost the GPU, resizes and reload included, under the validation layer:
		// a record, not a budget, which the benchmark measures (docs/build-and-gates.md).
		std::string passes;
		for (const Render::PassTiming &timing : m_passTimings)
			passes += std::format(" {} {:.2f}", timing.name, timing.meanMs);
		std::cerr << std::format("GPU ms per pass over {} frames:{}\n", m_measuredFrames, passes);
	}
	if (m_smokeSequence)
	{
		const Render::ClusterCheckTotals &checks = m_clusterChecks;
		std::cerr << "Cluster check: " << checks.frames << " frames compared, " << checks.litFrames
		          << " with lights, " << checks.failures << " disagreed; at most " << checks.peakEntries
		          << " of " << Render::kRendererLightIndexCapacity << " index entries and "
		          << checks.peakClusterLights << " lights in a cluster\n";
		if (checks.failures != 0)
			throw std::runtime_error(
			    "The cluster check failed in " + std::to_string(checks.failures) + " of " +
			    std::to_string(checks.frames) + " frames; the first: " + checks.firstFailure);
		if (checks.litFrames == 0)
			throw std::runtime_error(
			    "The cluster check compared " + std::to_string(checks.frames) +
			    " frames, and no cluster in any of them listed a light");
	}
}


void Application::Init()
{
	m_state = State::Initialising;
	InitUser();
	InitResources();
	InitScene();
	InitRenderer();
	InitWindow();
	// The headless run renders the dev tools, the demo window and the console: the UI path is part of
	// what the render test exercises, and an interactive run starts without it and asks for it with F1.
	m_showUI = m_arguments.smokeTest;
	m_devTools.showDemo = m_arguments.smokeTest;
	m_devTools.showConsole = m_arguments.smokeTest;
	m_showSceneDebug = m_arguments.smokeTest;
	m_state = State::Ready;
}


namespace
{
// The XDG specification has a directory it names made with mode 0700 when it does not exist yet.
void MakeUserDirectory(const std::filesystem::path &directory)
{
	if (std::filesystem::create_directories(directory))
		std::filesystem::permissions(directory, std::filesystem::perms::owner_all);
}
} // namespace

void Application::InitUser()
{
	std::filesystem::path root;
	if (m_arguments.userDirectory)
		root = *m_arguments.userDirectory;
	else if (m_arguments.smokeTest)
	{
		std::string scratch =
		    (std::filesystem::temp_directory_path() / "jettison-smoke-XXXXXX").string();
		if (!::mkdtemp(scratch.data()))
			throw std::runtime_error(
			    "Failed to make a user directory for the headless run in " + scratch);
		root = scratch;
		m_scratchUser = root;
	}
	m_userPaths = root.empty()
	                  ? Platform::ResolvePaths("jettison")
	                  : Platform::Paths{root / "data", root / "config", root / "state", root / "cache"};
	MakeUserDirectory(m_userPaths.userData);
	MakeUserDirectory(m_userPaths.config);
	m_settings.emplace(m_userPaths.config / "settings.yaml");
	m_settings->Load(m_input);
	m_glfwInput.SetJoysticks(&m_settings->Joysticks());
}


void Application::InitResources()
{
	if (m_assets.Mounts().empty())
	{
		m_assets.AddMount({"base", m_arguments.assetDirectory});
		m_assets.AddMount({"user", m_userPaths.userData, true});
	}
	m_pIoWorker = std::make_unique<Jobs::IoWorker>();
	m_pResources = std::make_unique<Resource::ResourceManager>(m_assets, *m_pIoWorker);
	Animation::RegisterRigLoader(*m_pResources);
	m_sceneUpdateSystem.emplace(m_pResources.get());
	Scene::RegisterJumpLoaders(*m_pResources);
	m_jump.emplace(*m_pResources, [this](Scene::JumpArrival &arrival) { ShowArrival(arrival); });
}


void Application::InitScene()
{
	m_pScene = std::make_unique<Scene::Scene>(m_pResources.get());
	InitGalaxy();
	DefineStarSystems();
	LoadScene();
	SpawnShip();
	if (!m_arguments.saveScenePath.empty())
		m_pScene->Save(m_arguments.saveScenePath);
}


void Application::InitRenderer()
{
	Render::RendererOptions options;
	options.assetDirectory = m_arguments.assetDirectory;
#ifndef NDEBUG
	options.requireDebugMessenger = m_arguments.smokeTest;
#endif
	options.clusterCheck = m_arguments.smokeTest;
	options.depthPrepass = m_arguments.depthPrepass;
	m_pRenderer = Render::CreateRenderer(m_assets, *m_pResources, std::move(options));
	m_pRenderer->Init();
}


void Application::InitWindow()
{
	GLFWwindow *const window = m_pRenderer->GetGLFWWindow();
	glfwSetWindowTitle(window, m_windowTitle.c_str());
	// The renderer has initialised the ImGui backend, whose callbacks the wheel's is installed over.
	m_glfwInput.Attach(window);
	if (m_arguments.smokeTest)
		glfwSetWindowSize(window, kSmokeWindowWidth, kSmokeWindowHeight);
	else if (m_arguments.windowWidth && m_arguments.windowHeight)
		glfwSetWindowSize(
		    window, static_cast<int>(*m_arguments.windowWidth),
		    static_cast<int>(*m_arguments.windowHeight));
}


void Application::RunFrameLoop()
{
	m_state = State::Running;
	// The sequence's frames, and its watchdog, start here: the window is on screen, the scene is
	// loaded, and the frames it counts are the frames of the run it is testing.
	if (m_arguments.smokeTest)
		m_smokeSequence = std::make_unique<SmokeSequence>(MakeSmokeActions(), CanIconifyWindow());
	m_frameClock.Tick();
	m_loopStart = Time::RealClock::Clock::now();

	while (!glfwWindowShouldClose(m_pRenderer->GetGLFWWindow()))
		Tick();

	m_clusterChecks = m_pRenderer->GetClusterChecks();
	m_passTimings = m_pRenderer->GetPassTimings();
	m_frameStats = m_pRenderer->GetFrameStats();
	m_measuredFrames = m_pRenderer->GetMeasuredFrames();
	m_deviceName = m_pRenderer->DeviceName();
	m_frameWidth = m_pRenderer->FrameWidth();
	m_frameHeight = m_pRenderer->FrameHeight();
	Shutdown();
}


void Application::Tick()
{
	JETTISON_PROFILE_FUNCTION();
	m_frameStart = Time::RealClock::Clock::now();
	const double deltaTime = m_frameClock.Tick(m_frameStart).count();
	for (const Ecs::TickPhase phase : Ecs::kTickPhases)
		m_frame.Run(phase, m_pScene->Registry(), deltaTime);
	JETTISON_PROFILE_FRAME();
}


void Application::DrawUI()
{
	m_pRenderer->BeginUi();
	if (m_showUI)
	{
		DevTools::DrawDevTools(m_console, m_devTools, &m_showUI);
		DrawRendererPanel();
		DrawScenePanel();
	}
	else if (m_devTools.showConsole)
		DevTools::DrawConsole(m_console, m_devTools.console, &m_devTools.showConsole);
	if (m_piloting)
		DrawFlight();
	DrawPicked();
	m_pRenderer->EndUi();
}


void Application::DrawScenePanel()
{
	if (!ImGui::Begin("Scene"))
	{
		ImGui::End();
		return;
	}
	ImGui::Text("%s", m_systemScene ? m_systemScene->System().name.c_str() : "No star system");
	ImGui::TextUnformatted("Layers");
	constexpr std::array<std::pair<Scene::Layer, const char *>, Scene::kLayerCount> kLayers{
	    {{Scene::Layer::Environment, "Environment"},
	     {Scene::Layer::Traffic, "Traffic"},
	     {Scene::Layer::Effects, "Effects"}}};
	for (const auto &[layer, name] : kLayers)
	{
		bool visible = m_pScene->IsLayerVisible(layer);
		if (ImGui::Checkbox(name, &visible))
			m_pScene->SetLayerVisible(layer, visible);
	}
	if (m_galaxy)
		DrawGalaxy();
	ImGui::End();
}


void Application::DrawGalaxy()
{
	constexpr std::array<const char *, 5> kStages{
	    "", "fading out", "reading the files", "streaming in", "fading in"};
	ImGui::Separator();
	ImGui::Text("Galaxy %s", m_galaxy->name.c_str());
	if (m_jump->Active())
		ImGui::Text(
		    "Jumping, %s (%.0f%%)", kStages[static_cast<std::size_t>(m_jump->Stage())],
		    100.0 * double{m_jump->Fade()});
	else if (!m_jump->Error().empty())
		ImGui::TextWrapped("%s", m_jump->Error().c_str());
	ImGui::BeginDisabled(m_jump->Active());
	if (m_shownSystem)
		for (const Celestial::Exit &exit : Celestial::Exits(*m_galaxy, *m_shownSystem))
		{
			const std::string label = std::format(
			    "Jump to {} through {}", m_galaxy->systems[exit.destination].name, exit.jumpPoint);
			if (ImGui::Button(label.c_str()))
				Jump(m_galaxy->systems[exit.destination].name);
		}
	// Straight to any system, along no lane: a developer's shortcut.
	for (std::size_t system = 0; system < m_galaxy->systems.size(); ++system)
	{
		if (system != 0)
			ImGui::SameLine();
		const std::string &name = m_galaxy->systems[system].name;
		ImGui::BeginDisabled(m_shownSystem == system);
		if (ImGui::SmallButton(name.c_str()))
			Jump(name);
		ImGui::EndDisabled();
	}
	ImGui::EndDisabled();
}


void Application::DrawRendererPanel()
{
	if (!ImGui::Begin("Renderer"))
	{
		ImGui::End();
		return;
	}
	const Render::FrameStats &stats = m_pRenderer->GetFrameStats();
	ImGui::Text(
	    "Meshes %u: %u outside the view, %u behind a horizon", stats.meshes, stats.frustumCulled,
	    stats.horizonCulled);
	ImGui::Text(
	    "View: %u draws, %llu triangles", stats.viewBatches,
	    static_cast<unsigned long long>(stats.viewTriangles));
	ImGui::Text(
	    "Shadow casters %u / %u / %u / %u, %llu triangles", stats.shadowCasters[0],
	    stats.shadowCasters[1], stats.shadowCasters[2], stats.shadowCasters[3],
	    static_cast<unsigned long long>(stats.shadowTriangles));
	ImGui::Text("Lights %u, %u listed", stats.lights, stats.listedLights);
	bool prepass = m_pRenderer->GetDepthPrepass();
	if (ImGui::Checkbox("Depth prepass", &prepass))
		m_pRenderer->SetDepthPrepass(prepass);
	ImGui::SameLine();
	if (ImGui::Button("Reset means"))
		m_pRenderer->ResetPassTimings();
	if (ImGui::BeginTable("passes", 3, ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("GPU pass");
		ImGui::TableSetupColumn("last ms");
		ImGui::TableSetupColumn("mean ms");
		ImGui::TableHeadersRow();
		for (const Render::PassTiming &timing : m_pRenderer->GetPassTimings())
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(timing.name.c_str());
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", timing.lastMs);
			ImGui::TableNextColumn();
			ImGui::Text("%.3f", timing.meanMs);
		}
		ImGui::EndTable();
	}
	ImGui::End();
}


void Application::AdvanceBenchmark()
{
	++m_benchmarkFrame;
	if (m_benchmarkFrame == kBenchmarkWarmUp)
	{
		m_pRenderer->ResetPassTimings();
		m_benchmarkStart = Time::RealClock::Clock::now();
	}
	else if (m_benchmarkFrame == kBenchmarkWarmUp + m_arguments.benchmarkFrames)
	{
		m_benchmarkSeconds =
		    std::chrono::duration<double>(Time::RealClock::Clock::now() - m_benchmarkStart).count();
		RequestClose();
	}
}


void Application::ReportBenchmark() const
{
	// Read after teardown from the copies RunFrameLoop took, as the cluster check's totals are.
	std::string passes;
	for (const Render::PassTiming &timing : m_passTimings)
		passes += std::format("  {:<16} {:8.3f} ms\n", timing.name, timing.meanMs);
	const Render::FrameStats &stats = m_frameStats;
	const double frames =
	    m_measuredFrames == 0 ? 1.0 : static_cast<double>(m_arguments.benchmarkFrames);
	std::cerr << std::format(
	    "Benchmark: {} on {} at {}x{}, {} frames after {} to warm up, {} GPU frames measured, depth "
	    "prepass {}\n"
	    "GPU time per pass (mean):\n{}"
	    "Wall time per frame: {:.3f} ms\n"
	    "Meshes {}: {} outside the view, {} behind a horizon; {} draws of {} triangles\n"
	    "Shadow casters {} / {} / {} / {} in {} / {} / {} / {} draws, {} triangles\n"
	    "Lights {}, {} listed\n",
	    m_arguments.systemPath.empty() ? m_arguments.scenePath.filename().string()
	                                   : m_arguments.systemPath,
	    m_deviceName, m_frameWidth, m_frameHeight, m_arguments.benchmarkFrames, kBenchmarkWarmUp,
	    m_measuredFrames, m_arguments.depthPrepass ? "on" : "off", passes,
	    m_benchmarkSeconds * 1000.0 / frames, stats.meshes, stats.frustumCulled, stats.horizonCulled,
	    stats.viewBatches, stats.viewTriangles, stats.shadowCasters[0], stats.shadowCasters[1],
	    stats.shadowCasters[2], stats.shadowCasters[3], stats.shadowBatches[0], stats.shadowBatches[1],
	    stats.shadowBatches[2], stats.shadowBatches[3], stats.shadowTriangles, stats.lights,
	    stats.listedLights);
}


void Application::PollInput()
{
	m_glfwInput.Feed(
	    m_pRenderer->GetGLFWWindow(), m_input,
	    ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse);
	// The console may have been closed by its window's own button last frame.
	SyncContexts();
	m_input.Poll();

	// On the press edge, not the level: a held key is one action, not every frame's action, and
	// the frame loop runs faster than a hand.
	if (m_input.WasPressedThisFrame(m_toggleUi))
		m_showUI = !m_showUI;
	if (m_input.WasPressedThisFrame(m_toggleDebugDraw))
		m_showSceneDebug = !m_showSceneDebug;
	// Each debug view's key shows it, or the shaded scene again when it is already showing.
	const auto toggleView = [this](Render::DebugView view)
	{
		m_pRenderer->SetDebugView(
		    m_pRenderer->GetDebugView() == view ? Render::DebugView::Shaded : view);
	};
	if (m_input.WasPressedThisFrame(m_toggleClusterHeat))
		toggleView(Render::DebugView::ClusterHeat);
	if (m_input.WasPressedThisFrame(m_toggleShadowCascades))
		toggleView(Render::DebugView::ShadowCascades);
	if (m_input.WasPressedThisFrame(m_cameraNext))
		NextCameraTarget(m_pScene->Registry());
	if (m_input.WasPressedThisFrame(m_jumpNext))
		Jump();
	if (m_input.WasPressedThisFrame(m_quickSave))
		SaveTo("quicksave");
	if (m_input.WasPressedThisFrame(m_quickLoad))
		LoadFrom("quicksave");
	// A click on the scene, not on a window over it, which ImGui has.
	if (m_input.WasPressedThisFrame(m_pickAction) &&
	    (ImGui::GetCurrentContext() == nullptr || !ImGui::GetIO().WantCaptureMouse))
		m_clicked = m_glfwInput.Cursor();
	if (m_input.WasPressedThisFrame(m_pilot))
	{
		if (m_piloting)
			StopPiloting();
		else
			StartPiloting(m_pilotView);
	}
	if (m_piloting && m_pilotInput->AssistToggled(m_input))
		m_assist = !m_assist;
	if (m_piloting && m_pilotInput->ViewToggled(m_input))
		m_pilotView = m_pilotView == Scene::PilotView::Chase ? Scene::PilotView::Cockpit
		                                                     : Scene::PilotView::Chase;
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
		else if (m_piloting)
			StopPiloting();
		else
			RequestClose();
	}
	SyncContexts();
}


void Application::LoadScene()
{
	// What the old system scene built goes with the scene's old contents.
	m_systemScene.reset();
	if (m_arguments.systemPath.empty())
		m_pScene->Load(m_arguments.scenePath);
	else
		m_pScene->LoadSystem(m_arguments.systemPath);
	if (const std::string &path = m_pScene->StarSystem(); !path.empty())
	{
		const auto bytes = m_assets.Read(path);
		if (!bytes)
			throw std::runtime_error("Failed to read the scene's star system '" + path + "'");
		m_systemScene.emplace(
		    *m_pScene,
		    Celestial::ReadStarSystem(
		        std::string_view{reinterpret_cast<const char *>(bytes->data()), bytes->size()}));
		// A scene of a system the galaxy does not list is the system physics knows of it.
		if (!m_pPhysics->HasStarSystem(Sim::SystemId(m_systemScene->System().name)))
			m_pPhysics->DefineStarSystem(m_systemScene->System());
		SetProxyFactory();
	}
	FindShownSystem();
	m_simStepper.Resync();
}


void Application::InitGalaxy()
{
	constexpr std::string_view kDefaultGalaxy = "systems/galaxy.yaml";
	const std::string path{m_arguments.galaxyPath.value_or(std::string{kDefaultGalaxy})};
	const auto bytes = m_assets.Read(path);
	if (!bytes)
	{
		if (m_arguments.galaxyPath)
			throw std::runtime_error("Failed to read the galaxy '" + path + "'");
		return;
	}
	try
	{
		m_galaxy.emplace(
		    Celestial::ReadGalaxy(
		        std::string_view{reinterpret_cast<const char *>(bytes->data()), bytes->size()}));
		m_galaxyPath = path;
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error("Galaxy '" + path + "': " + error.what());
	}
}


void Application::FindShownSystem()
{
	m_shownSystem.reset();
	m_arrivedAt.clear();
	if (!m_galaxy || !m_systemScene)
		return;
	const int index = m_galaxy->FindBySystemFile(m_pScene->StarSystem());
	if (index < 0)
		return;
	m_shownSystem = static_cast<std::size_t>(index);
	Celestial::CheckJumpPoints(*m_galaxy, *m_shownSystem, m_systemScene->System());
}


bool Application::Jump(std::string_view systemName)
{
	const auto refuse = [](std::string_view why)
	{
		Core::Log(Core::Severity::Warning, kShell, "No jump: {}", why);
		return false;
	};
	if (!m_galaxy)
		return refuse("there is no galaxy to jump in");
	if (m_jump->Active())
		return refuse("a jump is under way");

	const std::vector<Celestial::Exit> exits =
	    m_shownSystem ? Celestial::Exits(*m_galaxy, *m_shownSystem) : std::vector<Celestial::Exit>{};
	if (!systemName.empty())
	{
		const int system = m_galaxy->Find(systemName);
		if (system < 0)
			return refuse(std::format("the galaxy has no system '{}'", systemName));
		if (m_shownSystem && static_cast<std::size_t>(system) == *m_shownSystem)
			return refuse(std::format("'{}' is the system shown", systemName));
		const auto lane =
		    std::ranges::find(exits, static_cast<std::size_t>(system), &Celestial::Exit::destination);
		m_jumpStep = m_sim.Clock().Step();
		return m_jump->Begin(
		    *m_galaxy, static_cast<std::size_t>(system),
		    lane == exits.end() ? std::string{} : std::string{lane->arrival});
	}
	if (!m_shownSystem)
	{
		m_jumpStep = m_sim.Clock().Step();
		return m_jump->Begin(*m_galaxy, 0);
	}
	if (exits.empty())
		return refuse(std::format("no lane leaves '{}'", m_galaxy->systems[*m_shownSystem].name));
	// The exit after the one arrived by, which is the way the last jump came in.
	const auto arrived = std::ranges::find(exits, m_arrivedAt, &Celestial::Exit::jumpPoint);
	const Celestial::Exit &exit = arrived == exits.end() || std::next(arrived) == exits.end()
	                                  ? exits.front()
	                                  : *std::next(arrived);
	m_jumpStep = m_sim.Clock().Step();
	return m_jump->Begin(*m_galaxy, exit.destination, std::string{exit.arrival});
}


void Application::ShowArrival(Scene::JumpArrival &arrival)
{
	// What the old system scene built goes with the scene's old contents. The simulation did not
	// change, so the stepper is not resynced: the traffic layer's proxies are made from the same
	// snapshots, now for the entities in the new system. What was picked was in the old one.
	m_systemScene.reset();
	m_picked.reset();
	if (arrival.sceneFile)
		m_pScene->Load(*arrival.sceneFile);
	else
		m_pScene->LoadSystem(arrival.starSystemPath);
	m_systemScene.emplace(*m_pScene, std::move(arrival.starSystem));
	SetProxyFactory();
	// The ship stays where it is, so a jump leaves it behind: the simulation has no jump of its own
	// yet (docs/roadmap.md, "Carried forward").
	if (m_piloting && !CanPilot())
		StopPiloting();
	m_shownSystem = arrival.system;
	m_arrivedAt = arrival.arrival;
	if (arrival.arrival.empty())
		return;
	entt::registry &registry = m_pScene->Registry();
	for (const auto [body, celestial] : registry.view<const Scene::CelestialBodyComponent>().each())
		if (celestial.body == arrival.arrival)
		{
			for (const entt::entity camera : registry.view<const Scene::OrbitCameraComponent>())
				FrameBody(registry, camera, body);
			return;
		}
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
	// A notch of the wheel is an eighth nearer, or further.
	constexpr double kWheelRatio = 0.875;
	const double zoom = std::exp(-double{m_input.Axis(m_cameraZoom)} * kZoomRate * deltaTime) *
	                    std::pow(kWheelRatio, double{m_input.Vector(m_cameraWheel).x});
	for (const auto [entity, orbit] : registry.view<Scene::OrbitCameraComponent>().each())
	{
		orbit.rig.Orbit(turn.x, turn.y);
		orbit.rig.Zoom(zoom);
	}
	// In the seat, a drag looks about, and letting go looks ahead again.
	for (const auto [entity, pilot] : registry.view<Scene::PilotCameraComponent>().each())
	{
		if (m_input.IsPressed(m_cameraDrag))
		{
			const glm::dvec2 look = -glm::dvec2{m_input.Vector(m_cameraLook)} * kDragRate;
			pilot.cockpit.Look(look.x, look.y);
		}
		else
			pilot.cockpit.Centre();
	}
}


void Application::NextCameraTarget(entt::registry &registry)
{
	std::vector<entt::entity> bodies;
	for (const entt::entity entity : registry.view<const Scene::CelestialBodyComponent>())
		bodies.push_back(entity);
	if (bodies.empty())
		return;
	std::ranges::sort(bodies);
	for (const auto [entity, orbit] : registry.view<const Scene::OrbitCameraComponent>().each())
	{
		const auto current = std::ranges::find(bodies, orbit.target);
		FrameBody(
		    registry, entity,
		    current == bodies.end() || std::next(current) == bodies.end() ? bodies.front()
		                                                                  : *std::next(current));
	}
}


void Application::FrameBody(entt::registry &registry, entt::entity camera, entt::entity body)
{
	// Out to three of the body's radii, eased over two seconds from wherever the camera is.
	double radius = 50.0;
	if (m_systemScene)
	{
		const Celestial::StarSystem &system = m_systemScene->System();
		if (const int index = system.Find(registry.get<Scene::CelestialBodyComponent>(body).body);
		    index >= 0)
			radius = std::max(radius, system.bodies[static_cast<std::size_t>(index)].radius);
	}
	auto &orbit = registry.get<Scene::OrbitCameraComponent>(camera);
	if (const auto *pose = registry.try_get<Scene::CameraPoseComponent>(camera))
		orbit.transition =
		    Scene::CameraTransition{{pose->position, pose->orientation}, 2.0, Math::Easing::InOutCubic};
	orbit.target = body;
	orbit.minDistance = radius * 1.1;
	orbit.distance = radius * 3.0;
	orbit.placed = false;
}


void Application::SyncContexts()
{
	const bool flight = m_piloting;
	if (m_devTools.showConsole == m_consoleContext && flight == m_flightContext)
		return;
	// Down to the shell's, then up again in order, so the console is always on top.
	if (m_consoleContext)
		m_input.PopContext();
	if (m_flightContext)
		m_input.PopContext();
	if (flight)
		m_input.PushContext(PilotInput::kContext);
	if (m_devTools.showConsole)
		m_input.PushContext("console");
	m_flightContext = flight;
	m_consoleContext = m_devTools.showConsole;
}


void Application::RequestClose()
{
	glfwSetWindowShouldClose(m_pRenderer->GetGLFWWindow(), GLFW_TRUE);
}


void Application::SaveSettings()
{
	if (!m_settings)
		return;
	m_settings->Joysticks().NameBound(m_input, m_glfwInput.ConnectedJoysticks());
	m_settings->Save(m_input);
	Core::Log(Core::Severity::Info, kShell, "Settings saved to {}", m_settings->File().string());
}


bool Application::SaveTo(std::string_view name)
{
	try
	{
		const std::string path = SavePath(name);
		Sim::SaveGame save = Sim::SaveGame::Of(m_sim, std::string{name});
		save.info.writtenAt = std::chrono::duration_cast<std::chrono::seconds>(
		                          std::chrono::system_clock::now().time_since_epoch())
		                          .count();
		save.info.galaxy = m_galaxyPath;
		save.info.system = m_systemScene ? m_systemScene->System().name : std::string{};
		save.AddSection(
		    std::string{kShellSection},
		    WriteShellState({m_ship, m_station, m_port, m_shipSystem, m_stationBody}));
		const std::vector<std::byte> bytes = save.Write();
		m_assets.Write(path, bytes);
		Core::Log(
		    Core::Severity::Info, kShell, "Saved '{}' at step {}, {} bytes, to {}", name,
		    save.info.step, bytes.size(), (m_userPaths.userData / path).string());
		return true;
	}
	catch (const std::exception &error)
	{
		Core::Log(Core::Severity::Error, kShell, "Not saved: {}", error.what());
		return false;
	}
}


bool Application::LoadFrom(std::string_view name)
{
	try
	{
		const std::string path = SavePath(name);
		const auto bytes = m_assets.Read(path);
		if (!bytes)
			throw std::runtime_error(std::format("there is no save '{}'", name));
		const Sim::SaveGame save = Sim::SaveGame::Read(*bytes);
		if (save.info.galaxy != m_galaxyPath)
			throw std::runtime_error(
			    std::format(
			        "'{}' is of the galaxy '{}', and this run's is '{}'", name, save.info.galaxy,
			        m_galaxyPath));
		// Everything read before the world is replaced, so a save that does not fit changes nothing.
		const Sim::SaveSection *section = save.FindSection(kShellSection);
		const ShellState shell = section ? ReadShellState(section->data) : ShellState{};
		m_sim.Load(save.world);

		StopPiloting();
		const entt::registry &world = m_sim.Registry();
		const auto valid = [&world](entt::entity entity)
		{ return world.valid(entity) ? entity : entt::null; };
		m_ship = valid(shell.ship);
		m_station = valid(shell.station);
		m_port = valid(shell.port);
		m_shipSystem = shell.shipSystem;
		m_stationBody = shell.stationBody;
		m_docked = m_ship != entt::null && world.all_of<Docked>(m_ship);
		m_sentControls.reset();
		m_pickPending = false;
		m_picked.reset();
		m_simStepper.Resync();
		if (m_systemScene)
			m_systemScene->ResetTraffic();
		if (m_galaxy)
			for (const std::uint64_t system : UnknownSystems(world, *m_galaxy))
				Core::Log(
				    Core::Severity::Warning, kShell,
				    "'{}' has entities in a system {:#018x} the galaxy does not have", name, system);
		Core::Log(
		    Core::Severity::Info, kShell, "Loaded '{}' at step {}, saved by engine {}", name,
		    save.info.step, save.info.engineVersion);
		return true;
	}
	catch (const std::exception &error)
	{
		Core::Log(Core::Severity::Error, kShell, "Not loaded: {}", error.what());
		return false;
	}
}


SmokeActions Application::MakeSmokeActions()
{
	SmokeActions actions;
	actions.resize = [this](int width, int height)
	{ glfwSetWindowSize(m_pRenderer->GetGLFWWindow(), width, height); };
	actions.iconify = [this] { glfwIconifyWindow(m_pRenderer->GetGLFWWindow()); };
	actions.restore = [this] { glfwRestoreWindow(m_pRenderer->GetGLFWWindow()); };
	// The reload is the whole Load() path: the scene cleared and read again, so the renderer releases
	// what the new contents do not draw and keeps what they do. Every resource is read again too,
	// which is the hot-reload path a changed file takes - a model or texture uploaded over one a
	// frame in flight reads - without writing to the asset tree.
	actions.reload = [this]
	{
		LoadScene();
		m_pResources->ReloadAll();
	};
	actions.pilot = [this](bool cockpit)
	{ return StartPiloting(cockpit ? Scene::PilotView::Cockpit : Scene::PilotView::Chase); };
	actions.pick = [this]
	{
		int width = 0;
		int height = 0;
		glfwGetWindowSize(m_pRenderer->GetGLFWWindow(), &width, &height);
		return Pick({width * 0.5, height * 0.5});
	};
	actions.picking = [this] { return m_pickPending; };
	actions.saveAndLoad = [this]
	{
		const std::uint64_t saved = m_sim.StateHash();
		if (!SaveTo("smoke") || !LoadFrom("smoke"))
			return false;
		const std::uint64_t loaded = m_sim.StateHash();
		if (loaded != saved)
			Core::Log(
			    Core::Severity::Error, kShell, "The save loaded as {:#018x}, and was {:#018x} saved",
			    loaded, saved);
		return loaded == saved;
	};
	actions.jump = [this] { return Jump(); };
	actions.jumping = [this] { return m_jump->Active(); };
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
	// references, and the headless sequence walks that path on purpose. The renderer waits for the
	// frame loop's last frame before it releases anything.
	try
	{
		// The wheel's callback goes before the window does.
		m_glfwInput.Detach();
		if (m_pRenderer)
			m_pRenderer->Destroy();
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

	m_pRenderer.reset();
	// The system scene holds the scene, and goes first. The scene's rigs are resource handles,
	// released as it goes; then the manager, which holds no
	// GPU object once the renderer has released its handles, goes before the worker it posts to.
	m_systemScene.reset();
	m_pScene.reset();
	m_sceneUpdateSystem.reset();
	// The jump holds the handles of a destination's files while it reads them.
	m_jump.reset();
	m_pResources.reset();
	m_pIoWorker.reset();
	if (!m_scratchUser.empty())
	{
		std::error_code ignored;
		std::filesystem::remove_all(m_scratchUser, ignored);
	}
	// The smoke sequence is not reset: Run reads its verdict after this returns, and it holds
	// nothing of the engine's. Resetting it here once made that verdict unreachable, so a sequence
	// that never reached its last stage passed the gate.

	m_state = State::Stopped;
}


void Application::DefineStarSystems()
{
	if (!m_galaxy)
		return;
	for (const Celestial::GalaxySystem &entry : m_galaxy->systems)
	{
		const auto bytes = m_assets.Read(entry.system);
		if (!bytes)
			throw std::runtime_error("Failed to read star system '" + entry.system + "'");
		Celestial::StarSystem system = Celestial::ReadStarSystem(
		    std::string_view{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
		if (!m_pPhysics->HasStarSystem(Sim::SystemId(system.name)))
			m_pPhysics->DefineStarSystem(std::move(system));
	}
}


void Application::SpawnShip()
{
	if (m_ship != entt::null || !m_systemScene)
		return;
	const Celestial::StarSystem &system = m_systemScene->System();
	const auto station =
	    std::ranges::find(system.bodies, Celestial::BodyKind::Station, &Celestial::Body::kind);
	if (station == system.bodies.end())
		return;
	entt::registry &scene = m_pScene->Registry();
	entt::entity shown = entt::null;
	for (const auto [entity, celestial] : scene.view<const Scene::CelestialBodyComponent>().each())
		if (celestial.body == station->name)
			shown = entity;
	if (shown == entt::null)
		return;

	// The station's hull, as the scene draws it.
	const std::string shape = "stations/" + Scene::BodyEntityId(station->name);
	const std::optional<CollisionMesh> mesh = MakeCollisionMesh(scene, shown);
	if (!mesh)
	{
		Core::Log(
		    Core::Severity::Warning, kShell, "No ship: station '{}' is drawn with no shape to meet",
		    station->name);
		return;
	}
	if (!m_pPhysics->HasShape(shape))
	{
		if (mesh->skippedModels != 0)
			Core::Log(
			    Core::Severity::Warning, kShell,
			    "Station '{}' has {} models, which its collision mesh leaves out", station->name,
			    mesh->skippedModels);
		m_pPhysics->DefineShape(shape, mesh->shape);
		Core::Log(
		    Core::Severity::Info, kShell, "Station '{}' collides as {} triangles", station->name,
		    mesh->shape.indices.size() / 3);
	}
	if (!m_pPhysics->HasShape(kShipShape))
	{
		Physics::ShapeDefinition hull;
		hull.kind = Physics::ShapeKind::Box;
		hull.halfExtents = kShipHalfExtents;
		m_pPhysics->DefineShape(std::string{kShipShape}, hull);
	}

	// Where the station is now: placed by its rails from the next step, and by the shell for the
	// snapshots the stepper is resynced to.
	const double now = m_sim.Clock().Now();
	const auto index = static_cast<std::size_t>(station - system.bodies.begin());
	const Sim::Transform at{
	    Celestial::EvaluateBody(system, index, now).metres,
	    Celestial::Orientation(station->rotation, now)};
	const std::uint64_t systemId = Sim::SystemId(system.name);
	entt::registry &world = m_sim.Registry();

	m_station = world.create();
	world.emplace<Sim::Transform>(m_station, at);
	world.emplace<Sim::InSystem>(m_station, systemId);
	world.emplace<Physics::OnRails>(m_station, station->name);
	world.emplace<Physics::LocalFrame>(m_station, station->name);
	world.emplace<Physics::Body>(m_station, Physics::Body{Physics::BodyKind::Kinematic, shape});

	// Its docking port, before the dock at the hull's -Z end, the end the ship holds station off: a
	// sensor on rails at its place on the station, in the station's frame.
	double dockFace = 0.0;
	for (const glm::vec3 &vertex : mesh->shape.vertices)
		dockFace = std::min(dockFace, static_cast<double>(vertex.z));
	const glm::dvec3 portOffset{0.0, 0.0, dockFace - kPortHalfExtents.z + kPortOverlap};
	const std::string portShape = shape + "/port";
	if (!m_pPhysics->HasShape(portShape))
	{
		Physics::ShapeDefinition box;
		box.kind = Physics::ShapeKind::Box;
		box.halfExtents = kPortHalfExtents;
		m_pPhysics->DefineShape(portShape, box);
	}
	m_port = world.create();
	world.emplace<Sim::Transform>(m_port, at.position + at.orientation * portOffset, at.orientation);
	world.emplace<Sim::InSystem>(m_port, systemId);
	world.emplace<Physics::OnRails>(m_port, station->name, portOffset);
	world.emplace<Physics::LocalFrame>(m_port, station->name);
	Physics::Body sensor{Physics::BodyKind::Kinematic, portShape};
	sensor.sensor = true;
	world.emplace<Physics::Body>(m_port, sensor);
	world.emplace<DockingPort>(m_port, m_station);

	// Off the dock, nose towards it, still in the station's frame.
	m_ship = world.create();
	world.emplace<Sim::Transform>(
	    m_ship, at.position + at.orientation * kShipStation,
	    at.orientation * glm::angleAxis(glm::pi<double>(), glm::dvec3{0.0, 1.0, 0.0}));
	world.emplace<Sim::InSystem>(m_ship, systemId);
	world.emplace<Physics::LocalFrame>(m_ship, station->name);
	Physics::Body body{Physics::BodyKind::Dynamic, std::string{kShipShape}, kShipMass};
	body.friction = 0.4f;
	body.restitution = 0.2f;
	body.continuous = true;
	world.emplace<Physics::Body>(m_ship, body);
	world.emplace<Physics::Velocity>(m_ship);
	world.emplace<Physics::Thrusters>(m_ship);
	world.emplace<Physics::FlightControls>(m_ship, Physics::FlightControls{{}, {}, true, m_station});
	m_shipSystem = systemId;
	m_stationBody = station->name;
	m_simStepper.Resync();
	Core::Log(
	    Core::Severity::Info, kShell, "A ship holds station off {}'s dock: P flies it", station->name);
}


bool Application::CanPilot() const
{
	return m_ship != entt::null && m_systemScene &&
	       Sim::SystemId(m_systemScene->System().name) == m_shipSystem;
}


bool Application::StartPiloting(Scene::PilotView view)
{
	if (!CanPilot())
	{
		Core::Log(Core::Severity::Warning, kShell, "No ship to fly in the system shown");
		return false;
	}
	if (m_sim.Clock().Compression() > 1.0)
	{
		Core::Log(Core::Severity::Warning, kShell, "No piloting under time compression");
		return false;
	}
	m_piloting = true;
	m_pilotView = view;
	SyncContexts();
	return true;
}


void Application::StopPiloting()
{
	m_piloting = false;
	SyncContexts();
}


void Application::FlyShip()
{
	if (m_ship == entt::null)
		return;
	// A frame's input is a minute of flight at 60x: piloting is suspended under compression, as it is
	// in every game of the genre, and the assist holds the ship.
	if (m_piloting && m_sim.Clock().Compression() > 1.0)
		StopPiloting();
	Physics::FlightControls controls;
	if (m_piloting)
		controls = m_arguments.smokeTest ? kSmokeFlight : m_pilotInput->Read(m_input);
	// Let go, the ship holds station on its assist, whatever the pilot left it on.
	controls.assist = m_piloting ? m_assist : true;
	controls.reference = m_station;
	if (m_sentControls == controls)
		return;
	m_sim.Commands().Push(Physics::SetFlightControls{m_ship, controls});
	m_sentControls = controls;
}


void Application::SyncPilotCamera(entt::registry &registry)
{
	entt::entity camera = entt::null;
	for (const auto [entity, component] : registry.view<const Scene::CameraComponent>().each())
		if (component.active)
		{
			camera = entity;
			break;
		}
	if (camera == entt::null)
		return;
	const auto *pose = registry.try_get<Scene::CameraPoseComponent>(camera);
	auto *pilot = registry.try_get<Scene::PilotCameraComponent>(camera);
	if (!m_piloting)
	{
		if (!pilot)
			return;
		if (auto *orbit = registry.try_get<Scene::OrbitCameraComponent>(camera); orbit && pose)
			orbit->transition = Scene::CameraTransition{
			    {pose->position, pose->orientation}, kPilotTransition, Math::Easing::InOutCubic};
		registry.remove<Scene::PilotCameraComponent>(camera);
		return;
	}

	// The ship's proxy is made by the traffic layer's sync, once the ship is in the snapshots.
	entt::entity proxy = entt::null;
	for (const auto [entity, sim] : registry.view<const Scene::SimProxyComponent>().each())
		if (sim.simEntity == m_ship)
			proxy = entity;
	if (proxy == entt::null)
		return;
	if (!pilot)
	{
		pilot = &registry.emplace<Scene::PilotCameraComponent>(camera);
		// Behind and above the shuttle's ten metres, and in its seat, under the canopy.
		Scene::ChaseCameraSettings chase;
		chase.offset = {0.0, 4.5, 22.0};
		chase.lookAhead = 40.0;
		pilot->chase = Scene::ChaseCamera{chase};
		Scene::CockpitCameraSettings cockpit;
		cockpit.eye = {0.0, 1.0, -2.4};
		pilot->cockpit = Scene::CockpitCamera{cockpit};
		pilot->view = m_pilotView;
		if (pose)
			pilot->transition = Scene::CameraTransition{
			    {pose->position, pose->orientation}, kPilotTransition, Math::Easing::InOutCubic};
	}
	else if (pilot->view != m_pilotView)
	{
		if (pose)
			pilot->transition = Scene::CameraTransition{
			    {pose->position, pose->orientation}, kViewTransition, Math::Easing::InOutCubic};
		pilot->view = m_pilotView;
	}
	pilot->target = proxy;
	pilot->frame = entt::null;
	for (const auto [entity, celestial] : registry.view<const Scene::CelestialBodyComponent>().each())
		if (celestial.body == m_stationBody)
			pilot->frame = entity;
}


entt::entity Application::MakeShipProxy(entt::registry &registry)
{
	struct Part
	{
		glm::dvec3 at;
		glm::vec3 rotation;
		glm::vec3 scale;
		Scene::Shape shape;
		glm::vec3 color;
		std::string_view material;
	};
	// Nose along -Z, as the ship flies: a hull, a canopy over its fore end, wings aft, and two engines
	// whose nozzles glow. A cylinder's axis is its Y, turned onto Z a quarter turn about X.
	constexpr float kQuarter = 1.5707963f;
	const std::array<Part, 7> parts{{
	    {{0.0, 0.0, 0.0},
	     {},
	     {1.2f, 0.55f, 4.4f},
	     Scene::Shape::Box,
	     {0.7f, 0.7f, 0.74f},
	     "materials/hull.material.yaml"},
	    {{0.0, 0.5, -1.9}, {}, {0.75f, 0.45f, 1.3f}, Scene::Shape::IcoSphere, {0.08f, 0.1f, 0.16f}, ""},
	    {{0.0, -0.1, 1.6},
	     {},
	     {3.2f, 0.12f, 1.8f},
	     Scene::Shape::Box,
	     {0.55f, 0.56f, 0.6f},
	     "materials/hull.material.yaml"},
	    {{-0.75, 0.0, 4.2},
	     {kQuarter, 0.0f, 0.0f},
	     {0.42f, 0.6f, 0.42f},
	     Scene::Shape::Cylinder,
	     {0.3f, 0.3f, 0.32f},
	     ""},
	    {{0.75, 0.0, 4.2},
	     {kQuarter, 0.0f, 0.0f},
	     {0.42f, 0.6f, 0.42f},
	     Scene::Shape::Cylinder,
	     {0.3f, 0.3f, 0.32f},
	     ""},
	    {{-0.75, 0.0, 4.82},
	     {},
	     {0.34f, 0.34f, 0.06f},
	     Scene::Shape::IcoSphere,
	     {1.0f, 1.0f, 1.0f},
	     "materials/engine.material.yaml"},
	    {{0.75, 0.0, 4.82},
	     {},
	     {0.34f, 0.34f, 0.06f},
	     Scene::Shape::IcoSphere,
	     {1.0f, 1.0f, 1.0f},
	     "materials/engine.material.yaml"},
	}};
	const entt::entity root = registry.create();
	registry.emplace<Scene::TransformComponent>(root);
	for (const Part &part : parts)
	{
		const entt::entity entity = registry.create();
		registry.emplace<Scene::BuiltComponent>(entity);
		registry.emplace<Scene::ParentComponent>(entity, root);
		registry.emplace<Scene::TransformComponent>(entity, part.at, part.rotation, part.scale);
		Scene::MeshComponent mesh;
		mesh.shape = part.shape;
		mesh.color = part.color;
		mesh.material = std::string{part.material};
		registry.emplace<Scene::MeshComponent>(entity, std::move(mesh));
	}
	return root;
}


void Application::SetProxyFactory()
{
	m_systemScene->SetProxyFactory(
	    [this](entt::registry &registry, entt::entity simEntity)
	    { return simEntity == m_ship ? MakeShipProxy(registry) : entt::entity{entt::null}; });
}


void Application::DrawFlight()
{
	// Speeds from the snapshots, which are what presentation may see: the ship's motion relative to
	// the station's over the last step.
	const Sim::Snapshot &previous = m_simStepper.Previous();
	const Sim::Snapshot &current = m_simStepper.Current();
	const auto *ship = current.Find(m_ship);
	const auto *station = current.Find(m_station);
	if (!ship || !station)
		return;
	double speed = 0.0;
	if (const auto *shipBefore = previous.Find(m_ship), *stationBefore = previous.Find(m_station);
	    shipBefore && stationBefore && current.time > previous.time)
		speed = glm::length(
		            (ship->position - shipBefore->position) -
		            (station->position - stationBefore->position)) /
		        (current.time - previous.time);
	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(
	    {viewport->WorkPos.x + 12.0f, viewport->WorkPos.y + viewport->WorkSize.y - 12.0f},
	    ImGuiCond_Always, {0.0f, 1.0f});
	ImGui::SetNextWindowBgAlpha(0.45f);
	constexpr ImGuiWindowFlags kOverlay =
	    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
	    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if (ImGui::Begin("Flight", nullptr, kOverlay))
	{
		ImGui::Text(
		    "%s  %.1f m/s  %.0f m", m_stationBody.c_str(), speed,
		    glm::length(ship->position - station->position));
		if (m_docked)
			ImGui::Text("Docked at %s", m_stationBody.c_str());
		ImGui::Text(
		    "Assist %s (Z)  View %s (V)  P lets go", m_assist ? "on" : "off",
		    m_pilotView == Scene::PilotView::Chase ? "chase" : "cockpit");
		ImGui::TextDisabled("W/S throttle  A/D R/F strafe  arrows pitch, yaw  Q/E roll");
	}
	ImGui::End();
}


bool Application::Pick(const glm::dvec2 &point)
{
	if (!m_systemScene)
		return false;
	int width = 0;
	int height = 0;
	glfwGetWindowSize(m_pRenderer->GetGLFWWindow(), &width, &height);
	if (width <= 0 || height <= 0)
		return false;
	// The camera as it was drawn last frame, which is what was clicked on, and the time it showed.
	const Math::DRay ray = Render::ScreenRay(
	    Render::ActiveCameraView(m_pScene->Registry()), point, glm::dvec2{width, height});
	// From the seat, the ray starts inside the hull.
	const entt::entity ignore =
	    m_piloting && m_pilotView == Scene::PilotView::Cockpit ? m_ship : entt::entity{entt::null};
	m_pickTime = m_shownTime;
	m_sim.Commands().Push(
	    Physics::Pick{
	        ++m_pickRequest, Sim::SystemId(m_systemScene->System().name), ray.origin, ray.direction,
	        kPickDistance, m_pickTime, ignore});
	m_pickPending = true;
	return true;
}


void Application::TakeEvents()
{
	for (const Sim::SimEvent &event : m_sim.Events().Take())
	{
		if (const auto *picked = event.event.try_cast<Physics::Picked>())
		{
			// An answer to a pick since superseded is not what the mouse last asked about.
			if (picked->request != m_pickRequest)
				continue;
			m_pickPending = false;
			if (picked->entity == entt::null)
			{
				m_picked.reset();
				Core::Log(Core::Severity::Info, kShell, "Picked nothing");
				continue;
			}
			m_picked = PickedMark{
			    picked->entity, picked->distance, picked->point - PilotFrameOrigin(m_pickTime),
			    picked->normal};
			Core::Log(
			    Core::Severity::Info, kShell, "Picked {} at {:.0f} m", Describe(picked->entity),
			    picked->distance);
		}
		else if (const auto *docked = event.event.try_cast<ShipDocked>();
		         docked && docked->ship == m_ship)
		{
			m_docked = true;
			Core::Log(Core::Severity::Info, kShell, "The ship docked at {}", m_stationBody);
		}
		else if (const auto *left = event.event.try_cast<ShipUndocked>(); left && left->ship == m_ship)
		{
			m_docked = false;
			Core::Log(Core::Severity::Info, kShell, "The ship left {}'s dock", m_stationBody);
		}
	}
}


void Application::DrawPicked()
{
	if (!m_picked)
		return;
	// Where it was hit, carried with the station's frame, and the surface's normal there; a
	// marker that keeps its size on the screen, about a hundredth of the distance across.
	const glm::dvec3 at = PilotFrameOrigin(m_shownTime) + m_picked->local;
	const double size = std::max(0.25, m_picked->distance * 0.01);
	constexpr glm::vec4 kMarker{1.0f, 0.8f, 0.2f, 1.0f};
	m_debugDraw.Sphere(at, size, kMarker, 24, Render::DebugDepth::Overlay);
	m_debugDraw.Line(
	    at, at + glm::dvec3{m_picked->normal} * size * 4.0, kMarker, Render::DebugDepth::Overlay);

	const ImGuiViewport *viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(
	    {viewport->WorkPos.x + viewport->WorkSize.x - 12.0f, viewport->WorkPos.y + 12.0f},
	    ImGuiCond_Always, {1.0f, 0.0f});
	ImGui::SetNextWindowBgAlpha(0.45f);
	constexpr ImGuiWindowFlags kOverlay =
	    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
	    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
	if (ImGui::Begin("Picked", nullptr, kOverlay))
		ImGui::Text("%s  %.0f m", Describe(m_picked->entity).c_str(), m_picked->distance);
	ImGui::End();
}


std::string Application::Describe(entt::entity simEntity) const
{
	if (simEntity == m_ship)
		return "Ship";
	if (simEntity == m_station)
		return m_stationBody;
	if (simEntity == m_port)
		return m_stationBody + "'s docking port";
	return std::format("Entity {:#x}", entt::to_integral(simEntity));
}


glm::dvec3 Application::PilotFrameOrigin(double simSeconds) const
{
	if (m_stationBody.empty() || !CanPilot())
		return glm::dvec3{0.0};
	const Celestial::StarSystem &system = m_systemScene->System();
	const int index = system.Find(m_stationBody);
	return index < 0
	           ? glm::dvec3{0.0}
	           : Celestial::EvaluateBody(system, static_cast<std::size_t>(index), simSeconds).metres;
}
} // namespace Jettison::App
