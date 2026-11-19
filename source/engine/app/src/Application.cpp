#include "Application.h"

// The window's input and size; the renderer behind IRenderer owns the window itself.
#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <jettison/animation/ClipFile.h>
#include <jettison/animation/Components.h>
#include <jettison/animation/RigLoader.h>
#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>
#include <jettison/core/Version.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/platform/Crash.h>
#include <jettison/profile/Profile.h>
#include <jettison/render/Camera.h>
#include <jettison/render/IRenderer.h>
#include <jettison/render/Overlay.h>
#include <jettison/render/SceneView.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/scene/SimProxies.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/Snapshot.h>

#include "Saves.h"
#include <jettison/app/Game.h>
#include <jettison/app/Mounts.h>
#include <jettison/app/Reasons.h>

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
constexpr Core::Category kShell{"shell"};

// The player the shell's controls are for, in the first slot; their commands say so.
constexpr Sim::Controller kPilot{Sim::ControllerKind::Player, 0};
// How far a pick reaches: past any station a ship is flown by.
constexpr double kPickDistance = 50000.0;
// The game UI's faces: Roboto for text, Hack for figures, and Droid Sans for a glyph neither has.
constexpr std::array<std::pair<std::string_view, bool>, 3> kUiFonts{{
    {"fonts/Roboto-Medium.ttf", false},
    {"fonts/Hack-Regular.ttf", false},
    {"fonts/DroidSans.ttf", true},
}};

// The shell's music, played from the content where it has it.
constexpr std::string_view kMusic = "audio/music/drift.flac";

// The language the screens are in (StringTable), in the settings.
constexpr std::string_view kLanguageKey = "ui.language";


// A bus's volume in the settings: audio.master, audio.music, ...
std::string VolumeKey(Audio::Bus bus)
{
	return std::format("audio.{}", Audio::BusName(bus));
}


// Minimizing is X11's and the offscreen null platform's: on Wayland a minimized surface leaves the
// frame loop parked in DeviceContext::WaitOnWindowResized until the window is restored, and a headless
// run cannot arrange the restore from inside the loop it is parked in. The headless presets build
// GLFW's X11 backend, so the gated run takes the stage.
bool CanIconifyWindow()
{
	return glfwGetPlatform() == GLFW_PLATFORM_X11 || glfwGetPlatform() == GLFW_PLATFORM_NULL;
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

namespace
{
// The console's crash: a write through a null pointer, a failed assertion, or an exception thrown on
// a thread of its own, where nothing catches it. Each reaches the crash handler by its own path: the
// signal itself, the abort the assertion ends in, and std::terminate's.
void Crash(std::span<const std::string> arguments)
{
	const std::string_view how = arguments.empty() ? std::string_view{} : arguments[0];
	if (how == "fault")
	{
		Core::Log(Core::Severity::Warning, kShell, "Crashing: a write through a null pointer");
		volatile int *volatile pTarget = nullptr;
		*pTarget = 1;
	}
	else if (how == "assert")
	{
		Core::Log(Core::Severity::Warning, kShell, "Crashing: an assertion that fails");
		JETTISON_VERIFY(how == "a defect");
	}
	else if (how == "exception")
	{
		Core::Log(Core::Severity::Warning, kShell, "Crashing: an exception nothing catches");
		std::thread{[] { throw std::runtime_error{"the console asked for a crash"}; }}.join();
	}
	else
		Core::Log(Core::Severity::Warning, kShell, "crash takes fault, assert or exception");
}
} // namespace


Application::Application(AppArguments arguments, std::string windowTitle, Game *game)
    : m_arguments{std::move(arguments)}, m_windowTitle{std::move(windowTitle)},
      m_pGame{game != nullptr ? game : &m_noGame}
{
	m_toggleUi = m_input.DefineAction("toggle_ui", Input::ActionType::Button);
	m_toggleConsole = m_input.DefineAction("toggle_console", Input::ActionType::Button);
	m_back = m_input.DefineAction("back", Input::ActionType::Button);
	m_toggleDebugDraw = m_input.DefineAction("toggle_debug_draw", Input::ActionType::Button);
	m_toggleClusterHeat = m_input.DefineAction("toggle_cluster_heat", Input::ActionType::Button);
	m_toggleShadowCascades = m_input.DefineAction("toggle_shadow_cascades", Input::ActionType::Button);
	m_toggleAmbientAccess = m_input.DefineAction("toggle_ambient_access", Input::ActionType::Button);
	m_cameraOrbit = m_input.DefineAction("camera_orbit", Input::ActionType::Vector);
	m_cameraZoom = m_input.DefineAction("camera_zoom", Input::ActionType::Axis);
	m_cameraDrag = m_input.DefineAction("camera_drag", Input::ActionType::Button);
	m_cameraLook = m_input.DefineAction("camera_look", Input::ActionType::Vector);
	m_cameraNext = m_input.DefineAction("camera_next", Input::ActionType::Button);
	m_cameraWheel = m_input.DefineAction("camera_wheel", Input::ActionType::Vector);
	m_pickAction = m_input.DefineAction("pick", Input::ActionType::Button);
	m_toggleSaves = m_input.DefineAction("toggle_saves", Input::ActionType::Button);
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
	m_input.Bind("shell", m_toggleAmbientAccess, {Input::Keys::F6});
	using Input::Keys::Character;
	m_input.Bind("shell", m_cameraOrbit, {Character('d'), Character('a'), 1.0f, 0.0f, 0});
	m_input.Bind("shell", m_cameraOrbit, {Character('w'), Character('s'), 1.0f, 0.0f, 1});
	m_input.Bind("shell", m_cameraZoom, {Character('e'), Character('q')});
	m_input.Bind("shell", m_cameraDrag, {{Input::Device::MouseButton, 1}});
	m_input.Bind("shell", m_cameraLook, {{Input::Device::MouseAxis, 0}, {}, 1.0f, 0.0f, 0});
	m_input.Bind("shell", m_cameraLook, {{Input::Device::MouseAxis, 1}, {}, 1.0f, 0.0f, 1});
	m_input.Bind("shell", m_cameraNext, {Character('c')});
	m_input.Bind("shell", m_cameraWheel, {{Input::Device::MouseAxis, 3}});
	m_input.Bind("shell", m_pickAction, {{Input::Device::MouseButton, 0}});
	m_input.Bind("shell", m_toggleSaves, {Character('l')});
	m_input.Bind("shell", m_quickSave, {Input::Keys::F5});
	m_input.Bind("shell", m_quickLoad, {Input::Keys::F9});
	m_input.PushContext("shell");

	m_console.CaptureLog();
	m_console.RegisterCommand(
	    "quit", "Closes the window and ends the run",
	    [this](DevTools::Console &, std::span<const std::string>) { RequestClose(); });
	m_console.RegisterCommand(
	    "language",
	    "Says the language the screens are in and those there are tables for, or reads them in "
	    "another, "
	    "kept in the settings: language [tag]",
	    [this](DevTools::Console &console, std::span<const std::string> arguments)
	    { SetLanguage(console, arguments); });
	m_console.RegisterCommand(
	    "crash",
	    "Ends the shell as a defect would, to see the crash report it writes: crash fault | assert | "
	    "exception",
	    [](DevTools::Console &, std::span<const std::string> arguments) { Crash(arguments); });
	m_console.RegisterCommand(
	    "scripts", "Says which mods' scripts are loaded, how often each was called, and how it trapped",
	    [this](DevTools::Console &console, std::span<const std::string>)
	    {
		    const std::vector<Script::ScriptStatus> scripts = m_pScripts->Scripts();
		    if (scripts.empty())
			    console.Print("No scripts are loaded");
		    for (const Script::ScriptStatus &script : scripts)
			    console.Print(
			        std::format(
			            "{}: {} call(s), {} trap(s){}{}", script.name, script.calls, script.traps,
			            script.stopped ? ", stopped" : "",
			            script.lastTrap.empty() ? "" : std::format(" (last: {})", script.lastTrap)));
	    });
	m_console.RegisterCommand(
	    "script_bindings",
	    "Writes the bindings a mod's scripts are built against, from this build's types, into a "
	    "directory: jettison_script.h for C and jettison_script.rs for Rust: script_bindings "
	    "<directory>",
	    [this](DevTools::Console &console, std::span<const std::string> arguments)
	    {
		    if (arguments.size() != 1)
			    throw std::invalid_argument{"takes the directory to write into"};
		    const std::filesystem::path directory{arguments[0]};
		    std::filesystem::create_directories(directory);
		    for (const auto &[name, text] :
		         {std::pair{"jettison_script.h", m_pScripts->WriteHeader()},
		          std::pair{"jettison_script.rs", m_pScripts->WriteRustModule()}})
		    {
			    std::ofstream file{directory / name, std::ios::binary};
			    file << text;
			    if (!file)
				    throw std::runtime_error{
				        std::format("could not write {}", (directory / name).string())};
		    }
		    console.Print(
		        std::format(
		            "Wrote jettison_script.h and jettison_script.rs into {}", directory.string()));
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
				    const std::string_view system = info.Detail(kPlaceDetail);
				    console.Print(
				        std::format(
				            "{}: step {} in {}, by engine {}", name, info.step,
				            system.empty() ? "no system" : system, info.engineVersion));
			    }
			    catch (const std::exception &error)
			    {
				    console.PrintError(std::format("{}: {}", name, error.what()));
			    }
		    }
	    });

	m_console.RegisterCommand(
	    "volume",
	    "Sets a bus's volume and saves the settings, or says what the volumes are: volume [bus] [0 to "
	    "4], a bus being master, music, effects, ui or voice",
	    [this](DevTools::Console &console, std::span<const std::string> arguments)
	    {
		    if (!m_pAudio)
			    return;
		    if (arguments.empty())
		    {
			    for (const Audio::Bus bus : Audio::kBuses)
				    console.Print(
				        std::format("{} {:.2f}", Audio::BusName(bus), m_pAudio->BusVolume(bus)));
			    return;
		    }
		    const std::optional<Audio::Bus> bus = Audio::BusFromName(arguments[0]);
		    if (!bus)
		    {
			    console.PrintError(std::format("No bus is called '{}'", arguments[0]));
			    return;
		    }
		    if (arguments.size() > 1)
		    {
			    float volume = 0.0f;
			    const std::string &text = arguments[1];
			    const auto [end, error] =
			        std::from_chars(text.data(), text.data() + text.size(), volume);
			    if (error != std::errc{} || end != text.data() + text.size() || !(volume >= 0.0f))
			    {
				    console.PrintError(std::format("'{}' is not a volume", text));
				    return;
			    }
			    m_pAudio->SetBusVolume(*bus, volume);
			    m_settings->Values().SetNumber(
			        Core::ConfigLayer::User, VolumeKey(*bus), m_pAudio->BusVolume(*bus));
			    SaveSettings();
		    }
		    console.Print(std::format("{} {:.2f}", Audio::BusName(*bus), m_pAudio->BusVolume(*bus)));
	    });
	m_console.RegisterCommand(
	    "music", "Starts or stops the music: music [on|off]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    if (!m_pAudio)
			    return;
		    const bool on = arguments.empty() ? !m_pAudio->IsPlaying(m_music) : arguments[0] != "off";
		    if (on)
			    m_music = m_pAudio->PlayMusic(kMusic);
		    else
			    m_pAudio->StopMusic();
	    });
	m_console.RegisterCommand(
	    "screenshot", "Writes the next frame to a PNG, with the UI over it: screenshot <file.png>",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    if (arguments.size() != 1)
			    throw std::invalid_argument{"takes the file to write"};
		    m_pRenderer->RequestScreenshot(std::filesystem::absolute(arguments[0]));
	    });
	m_console.RegisterCommand(
	    "view",
	    "Shows the shaded scene or a debug view of it, as F3, F4 and F6 do: view "
	    "shaded|clusters|cascades|occlusion",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    constexpr std::array<std::pair<std::string_view, Render::DebugView>, 4> kViews{
		        {{"shaded", Render::DebugView::Shaded},
		         {"clusters", Render::DebugView::ClusterHeat},
		         {"cascades", Render::DebugView::ShadowCascades},
		         {"occlusion", Render::DebugView::AmbientAccess}}};
		    const auto found = std::ranges::find_if(
		        kViews,
		        [&](const auto &view) { return !arguments.empty() && view.first == arguments[0]; });
		    if (found == kViews.end())
			    throw std::invalid_argument{"takes shaded, clusters, cascades or occlusion"};
		    m_pRenderer->SetDebugView(found->second);
	    });
	m_console.RegisterCommand(
	    "sequence",
	    "Plays a cinematic in cinematics/ from a time in seconds, over the ship, its pad and its "
	    "station, or stops the one playing: sequence <name> [seconds] | stop",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    if (arguments.empty() || arguments.size() > 2)
			    throw std::invalid_argument{"takes a sequence's name and a time, or stop"};
		    if (arguments[0] == "stop")
		    {
			    StopSequence();
			    return;
		    }
		    double from = 0.0;
		    if (arguments.size() == 2)
		    {
			    const std::string &text = arguments[1];
			    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), from);
			    if (error != std::errc{} || end != text.data() + text.size() || !(from >= 0.0))
				    throw std::invalid_argument{std::format("'{}' is not a time", text)};
		    }
		    PlaySequence(arguments[0], from);
	    });
	m_console.RegisterCommand(
	    "grade",
	    "Grades the frame by a .cube's table in grades/, by a strength from 0 to 1, or stops grading "
	    "it: grade <name> [strength] | off",
	    [this](DevTools::Console &, std::span<const std::string> arguments) { SetGrade(arguments); });
	m_console.RegisterCommand(
	    "orbit",
	    "Places the orbit camera at once, in radians and metres, about the target named or the one "
	    "it is on: orbit <yaw> <pitch> <distance> [target]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { PlaceOrbitCamera(arguments); });

	DevTools::RegisterThemeTypes();
	// The dev tools' palette, scale and mono console, as variables of their own: a dev on a 4K panel
	// and one on a laptop settle for one run's settings each, without a rebuild.
	m_console.BindVariables("dev", m_theme);

	Sim::RegisterTypes();
	m_sim.RegisterComponent<Sim::Transform>();
	m_sim.RegisterComponent<Sim::InFrame>();
	m_pPhysics = &Physics::PhysicsWorld::Attach(m_sim);
	m_sim.RegisterComponent<Sim::ControlledBy>();
	// For what a reason says of an entity on rails, by its body's name (Reasons.h).
	m_sim.ShowInSnapshots<Physics::OnRails>();
	// For the frame an entity's physics is in, which a game's presentation reads.
	m_sim.ShowInSnapshots<Physics::LocalFrame>();
	RegisterSavesScreenTypes();
	m_pGame->Register(MakeShellWorld(), *this);
	m_pGame->AddCommands(m_console, *this);
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

	// The mouse's picks, as the commands the next step applies; a game's commands go here too.
	m_frame.Register(
	    {"shell.pick",
	     TickPhase::Commands,
	     {},
	     {},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("shell.pick");
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

	// The shell's systems in each phase name the one before them, so a game's system put between two
	// of them (Game::RegisterFrame) leaves the rest in the order they are registered in here.
	m_frame.Register(
	    {"sim.sync",
	     TickPhase::Sync,
	     Access::Of<Scene::SimProxyComponent>().Writing<TransformComponent>(),
	     {},
	     [this](entt::registry &registry, double)
	     {
		     JETTISON_PROFILE_ZONE("sim.sync");
		     const Sim::Snapshot &previous = m_simStepper.Previous();
		     const Sim::Snapshot &current = m_simStepper.Current();
		     m_shownTime = previous.time + (current.time - previous.time) * m_simStepper.Alpha();
		     // A game that keeps the proxies itself makes and unmakes them; otherwise the scene shows
		     // only the proxies it has.
		     if (!m_pGame->SyncProxies(*this))
			     Scene::SyncSimProxies(registry, previous, current, m_simStepper.Alpha());
	     }});
	// Every frame, so the queue does not grow with events nothing turns into sound or UI; a game's
	// handlers replace this system.
	m_frame.Register(
	    {"sim.events",
	     TickPhase::Sync,
	     {},
	     {"sim.sync"},
	     [this](entt::registry &, double) { TakeEvents(); }});

	m_frame.Register(
	    {"scene.update",
	     TickPhase::Presentation,
	     Access::Of<TransformComponent, ParentComponent, MeshComponent>()
	         .Writing<
	             RotationAnimationComponent, AnimationPlayerComponent, RigComponent, SkeletonComponent,
	             Scene::ClipPlayerComponent, Scene::NodePoseComponent, WorldTransformComponent>(),
	     {},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("scene.update");
		     m_clipEvents.clear();
		     m_sceneUpdateSystem->Update(registry, static_cast<float>(deltaTime), &m_clipEvents);
	     }});
	// After the world transforms, which the rigs follow, and before anything that reads the pose.
	m_frame.Register(
	    {"camera.rig",
	     TickPhase::Presentation,
	     Access::Of<WorldTransformComponent>()
	         .Writing<
	             Scene::OrbitCameraComponent, Scene::PilotCameraComponent,
	             Scene::CameraPoseComponent>(),
	     {"scene.update"},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("camera.rig");
		     DriveCameras(registry, deltaTime);
		     Scene::UpdateOrbitCameras(registry, deltaTime);
		     Scene::UpdatePilotCameras(registry, deltaTime);
	     }});
	// A cinematic over where the rigs left the camera, and what it shows over the frame.
	m_frame.Register(
	    {"camera.sequence",
	     TickPhase::Presentation,
	     Access::Of<WorldTransformComponent, CameraComponent, Scene::SimProxyComponent>()
	         .Writing<Scene::SequencePlayerComponent, Scene::CameraPoseComponent>(),
	     {"camera.rig"},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("camera.sequence");
		     m_sequences->Update(registry, deltaTime);
		     ShowSequence(registry);
	     }});
	// After the cameras, so the listener is where this frame's camera is, and after the world
	// transforms the sources follow.
	m_frame.Register(
	    {"audio.update",
	     TickPhase::Presentation,
	     Access::Of<WorldTransformComponent, Scene::CameraPoseComponent>()
	         .Writing<Audio::SoundComponent>(),
	     {"camera.sequence", "scene.update"},
	     [this](entt::registry &registry, double)
	     {
		     JETTISON_PROFILE_ZONE("audio.update");
		     UpdateAudio(registry);
	     }});
	m_frame.Register(
	    {"scene.debug",
	     TickPhase::Presentation,
	     Access::Of<WorldTransformComponent, MeshComponent, LightComponent>(),
	     {"audio.update"},
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
	     }});
	// Before the render, so a texture or material that finished loading, or reloaded, draws this
	// frame.
	m_frame.Register(
	    {"resource.update",
	     TickPhase::Presentation,
	     {},
	     {"scene.debug"},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("resource.update");
		     m_pResources->Update();
	     }});
	m_frame.Register(
	    {"shell.ui",
	     TickPhase::Presentation,
	     {},
	     {"resource.update"},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("shell.ui");
		     DrawUI();
	     }});
	// What is picked outlined, on the proxies the sync phase left (MarkScene).
	m_frame.Register(
	    {"shell.marks",
	     TickPhase::Presentation,
	     Access::Of<Scene::SimProxyComponent>().Writing<Scene::OutlineComponent>(),
	     {"shell.ui"},
	     [this](entt::registry &registry, double) { MarkScene(registry); }});
	// The game UI, from the snapshots the sync phase took in: the market screen's data, then layout
	// and drawing into the overlay.
	m_frame.Register(
	    {"ui.update",
	     TickPhase::Presentation,
	     {},
	     {"shell.marks"},
	     [this](entt::registry &, double)
	     {
		     JETTISON_PROFILE_ZONE("ui.update");
		     UpdateGameUi();
	     }});

	m_frame.Register(
	    {"render",
	     TickPhase::Render,
	     Access::Of<
	         MeshComponent, WorldTransformComponent, SkeletonComponent, CameraComponent, LightComponent,
	         Scene::OutlineComponent, Scene::MaskComponent>(),
	     {},
	     [this](entt::registry &, double)
	     {
		     // The answer says whether this frame recreated the swapchain, which is the only sign
		     // of a handled resize the headless sequence can read: it has no way to see a window
		     // that resized without the engine noticing, and a resize the engine did not recreate
		     // a swapchain for is not one it handled.
		     const double presentationTime =
		         std::chrono::duration<double>(m_frameStart - m_loopStart).count();
		     static const Render::Overlay kNoOverlay;
		     m_frameResized = m_pRenderer->Render(
		         *m_pScene, m_debugDraw, m_pUi ? m_pUi->Overlay() : kNoOverlay, presentationTime);
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

	m_pGame->RegisterFrame(m_frame, *this);
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
		Core::Log(
		    Core::Severity::Info, kShell, "GPU ms per pass over {} frames:{}", m_measuredFrames,
		    passes);
	}
	// A document RmlUi complained of, a stylesheet property it does not know or a variable it could
	// not find, draws on regardless; the gate does not let it.
	if (m_smokeSequence && m_uiProblems != 0)
		throw std::runtime_error(
		    "The game UI logged " + std::to_string(m_uiProblems) +
		    " warnings and errors (see the log)");
	// A sound that would not play, or a warning from miniaudio, is a sound the player would not hear.
	if (m_smokeSequence && m_audioProblems != 0)
		throw std::runtime_error(
		    "The audio logged " + std::to_string(m_audioProblems) +
		    " warnings and errors (see the log)");
	if (m_smokeSequence)
	{
		const Render::ClusterCheckTotals &checks = m_clusterChecks;
		Core::Log(
		    Core::Severity::Info, kShell,
		    "Cluster check: {} frames compared, {} with lights, {} disagreed; at most {} of {} "
		    "index entries and {} lights in a cluster",
		    checks.frames, checks.litFrames, checks.failures, checks.peakEntries,
		    Render::kRendererLightIndexCapacity, checks.peakClusterLights);
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
	InitUi();
	InitAudio();
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
	// Before anything else of the engine starts, so a crash in its start-up is reported too. The state
	// directory is the logs' (Platform::Paths).
	Platform::InstallCrashHandler(
	    {m_userPaths.state / "crashes",
	     std::format("{}, engine {}", m_windowTitle, Core::EngineVersion()),
	     {},
	     true});
	MakeUserDirectory(m_userPaths.userData);
	MakeUserDirectory(m_userPaths.config);
	m_settings.emplace(m_userPaths.config / "settings.yaml");
	m_settings->Load(m_input);
	m_glfwInput.SetJoysticks(&m_settings->Joysticks());
}


void Application::InitResources()
{
	if (m_assets.Mounts().empty())
		m_mods = MountAssets(
		    m_assets, m_arguments.assets, m_arguments.content, m_userPaths.userData,
		    m_userPaths.config / kModListFile);
	m_pIoWorker = std::make_unique<Jobs::IoWorker>();
	// A fixed step's frames are the same every run only if each load ends on a frame the count sets,
	// and a reload would change one mid-run.
	Resource::ResourceOptions resourceOptions;
	if (m_arguments.frameStep > 0.0)
		resourceOptions = {.hotReload = false, .settleLoads = true};
	m_pResources = std::make_unique<Resource::ResourceManager>(m_assets, *m_pIoWorker, resourceOptions);
	Animation::RegisterRigLoader(*m_pResources);
	Animation::RegisterClipFileLoader(*m_pResources);
	m_definitions.emplace(m_assets);
	m_pGame->LoadDefinitions(*m_definitions);
	Script::LoadDefinitions(*m_definitions);
	m_pScripts = std::make_unique<Script::ScriptHost>(m_assets);
	m_pScripts->SendCommandsTo(m_sim);
	m_pScripts->LoadAll(*m_definitions);
	m_pGame->Define(MakeShellWorld(), *m_definitions);
	m_sceneUpdateSystem.emplace(m_pResources.get());
	Scene::RegisterSequenceLoader(*m_pResources);
	m_sequences.emplace(*m_pResources);
}


void Application::InitScene()
{
	m_pScene = std::make_unique<Scene::Scene>(m_pResources.get());
	LoadScene();
	if (!m_arguments.saveScenePath.empty())
		m_pScene->Save(m_arguments.saveScenePath);
}


void Application::InitRenderer()
{
	Render::RendererOptions options;
#ifndef NDEBUG
	options.requireDebugMessenger = m_arguments.smokeTest;
#endif
	options.clusterCheck = m_arguments.smokeTest;
	options.depthPrepass = m_arguments.depthPrepass;
	options.ambientOcclusion = m_arguments.ambientOcclusion;
	options.bloom = m_arguments.bloom;
	options.gpu = m_arguments.gpu;
	options.drawEveryFrame = m_arguments.frameStep > 0.0;
	options.offscreen = m_arguments.offscreen;
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


void Application::InitUi()
{
	m_pUi = std::make_unique<Ui::UiSystem>(
	    m_assets, static_cast<int>(m_pRenderer->FrameWidth()),
	    static_cast<int>(m_pRenderer->FrameHeight()));
	for (const auto &[font, fallback] : kUiFonts)
		m_pUi->LoadFont(font, fallback);
	// The player's language, or the base's: the setting, and not the environment's locale, so a run
	// reads the same on every machine until the player chooses.
	m_pUi->SetLanguage(m_settings->Values()
	                       .GetString(kLanguageKey)
	                       .value_or(std::string{Ui::StringTable::kBaseLanguage}));

	Ui::DataModel &saves = m_pUi->CreateDataModel("saves");
	m_pSavesModel = &saves;
	saves.Bind("screen", &m_savesScreen);
	saves.BindEvent(
	    "load",
	    [this](std::span<const Ui::EventArgument> arguments)
	    {
		    if (!arguments.empty() && LoadFrom(Ui::AsText(arguments[0])))
			    ShowSaves(false);
	    });
	m_savesDocument = m_pUi->LoadDocument("ui/saves.rml");
	// The headless run shows the screens from its first frame, so the overlay's path is drawn in every
	// stage the render test walks, and the save stage lists its save on one of them.
	ShowSaves(m_arguments.smokeTest);
	m_pGame->OpenUi(*m_pUi, *this);
}


void Application::InitAudio()
{
	Audio::AudioOptions options;
	options.output =
	    m_arguments.smokeTest || m_arguments.noSound ? Audio::Output::Null : Audio::Output::Device;
	m_pAudio = std::make_unique<Audio::AudioSystem>(m_assets, options);
	for (const Audio::Bus bus : Audio::kBuses)
		m_pAudio->SetBusVolume(bus, SettingVolume(bus));

	// After the docking and trading events are registered, in the constructor: a definition naming an
	// event Reflect does not know is refused here, by name.
	Audio::RegisterTypes();
	m_definitions->Load<Audio::EventSound>(
	    Audio::kEventSoundDirectory, Serialise::FormatVersion{Audio::kEventSoundVersion});
	m_eventSounds.emplace(*m_definitions);
	Core::Log(
	    Core::Severity::Info, kShell, "{} event sounds for {} events", m_eventSounds->Sounds(),
	    m_eventSounds->Events());
	// Content with no music, as the engine with none runs, starts in silence and says nothing.
	if (m_assets.Find(kMusic))
		m_music = m_pAudio->PlayMusic(kMusic);
}


float Application::SettingVolume(Audio::Bus bus)
{
	if (!m_settings)
		return 1.0f;
	return static_cast<float>(m_settings->Values().GetNumber(VolumeKey(bus)).value_or(1.0));
}


std::uint64_t Application::UiSounds() const
{
	return m_pAudio ? m_pAudio->Counters().started[static_cast<std::size_t>(Audio::Bus::Ui)] : 0;
}


void Application::UpdateAudio(entt::registry &registry)
{
	if (!m_pAudio)
		return;
	const Render::CameraView view = Render::ActiveCameraView(registry);
	m_pAudio->SetListener(view.position, view.orientation);
	// The clips' events this frame, each heard from its player: a pad's clamps closing on a ship and
	// letting it go, wherever the ship is from.
	if (m_eventSounds)
		for (const Scene::ClipEventFired &event : m_clipEvents)
		{
			const auto *world = registry.valid(event.player)
			                        ? registry.try_get<Scene::WorldTransformComponent>(event.player)
			                        : nullptr;
			m_eventSounds->PlayClipEvent(
			    event.name, world ? std::optional{glm::dvec3{world->matrix[3]}} : std::nullopt,
			    *m_pAudio);
		}
	m_soundSources.Update(registry, *m_pAudio);
	m_pAudio->Update();
}


void Application::PlayEventSound(const Sim::SimEvent &event)
{
	if (!m_pAudio || !m_eventSounds)
		return;
	if (!m_pGame->Hears(event))
		return;
	// Where presentation shows the entity this frame: between the last two steps, as the scene does.
	m_eventSounds->Play(
	    event, *m_pAudio,
	    [this](entt::entity entity) -> std::optional<glm::dvec3>
	    {
		    const std::optional<Sim::Transform> at = Sim::Interpolate(
		        m_simStepper.Previous(), m_simStepper.Current(), entity, m_simStepper.Alpha());
		    return at ? std::optional{at->position} : std::nullopt;
	    });
}


void Application::ShowSaves(bool shown)
{
	m_showSaves = shown;
	if (!m_pUi)
		return;
	if (!shown)
	{
		m_pUi->Hide(m_savesDocument);
		return;
	}
	SavesScreen screen = BuildSavesScreen(m_assets);
	if (screen != m_savesScreen)
	{
		m_savesScreen = std::move(screen);
		m_pSavesModel->Changed("screen");
	}
	m_pUi->Show(m_savesDocument);
}


void Application::FeedGameUi()
{
	if (!m_pUi)
		return;
	GLFWwindow *const window = m_pRenderer->GetGLFWWindow();
	const std::vector<WindowEvent> events = m_glfwInput.TakeEvents();
	const bool imguiMouse = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
	const bool imguiKeyboard =
	    ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;

	// The UI is laid out in the frame's pixels, and the cursor is in the window's, which differ on a
	// scaled display.
	int windowWidth = 0;
	int windowHeight = 0;
	glfwGetWindowSize(window, &windowWidth, &windowHeight);
	const glm::dvec2 scale{
	    windowWidth > 0 ? static_cast<double>(m_pRenderer->FrameWidth()) / windowWidth : 1.0,
	    windowHeight > 0 ? static_cast<double>(m_pRenderer->FrameHeight()) / windowHeight : 1.0};
	glm::dvec2 cursor{0.0};
	glfwGetCursorPos(window, &cursor.x, &cursor.y);
	const Ui::Modifiers none{};
	if (imguiMouse)
		m_pUi->MouseLeave();
	else
		m_pUi->MouseMove(glm::vec2{cursor * scale}, none);

	for (const WindowEvent &event : events)
	{
		const Ui::Modifiers modifiers{event.shift, event.control, event.alt};
		switch (event.kind)
		{
			case WindowEvent::Kind::MouseButton:
				if (!imguiMouse)
					m_pUi->MouseButton(event.code, event.pressed, modifiers);
				break;
			case WindowEvent::Kind::Wheel:
				if (!imguiMouse)
					m_pUi->MouseWheel(event.wheel, none);
				break;
			case WindowEvent::Kind::Key:
				if (!imguiKeyboard)
					m_pUi->Key({Input::Device::Keyboard, event.code}, event.pressed, modifiers);
				break;
			case WindowEvent::Kind::Text:
				if (!imguiKeyboard)
					m_pUi->Text(event.character);
				break;
		}
	}
}


void Application::UpdateGameUi()
{
	if (!m_pUi)
		return;
	const glm::ivec2 size{
	    static_cast<int>(m_pRenderer->FrameWidth()), static_cast<int>(m_pRenderer->FrameHeight())};
	if (size != m_uiSize)
	{
		float scaleX = 1.0f;
		float scaleY = 1.0f;
		glfwGetWindowContentScale(m_pRenderer->GetGLFWWindow(), &scaleX, &scaleY);
		m_pUi->SetSize(size.x, size.y, scaleY);
		m_uiSize = size;
	}

	m_pGame->UpdateUi(*this);
	const double presentationTime = std::chrono::duration<double>(m_frameStart - m_loopStart).count();
	m_pUi->Update(presentationTime);
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
	m_frameStart = m_loopStart;
	for (const RunCommand &command : m_arguments.commands)
		if (command.frame == 0 && !command.simulationSeconds)
			RunCommandLine(command.line);
		else
			m_pendingCommands.push_back(command);

	while (!glfwWindowShouldClose(m_pRenderer->GetGLFWWindow()))
		Tick();

	m_clusterChecks = m_pRenderer->GetClusterChecks();
	m_passTimings = m_pRenderer->GetPassTimings();
	m_benchmarkAmbientOcclusion = m_pRenderer->GetAmbientOcclusion();
	m_frameStats = m_pRenderer->GetFrameStats();
	m_measuredFrames = m_pRenderer->GetMeasuredFrames();
	m_deviceName = m_pRenderer->DeviceName();
	m_frameWidth = m_pRenderer->FrameWidth();
	m_frameHeight = m_pRenderer->FrameHeight();
	m_uiProblems = m_pUi ? m_pUi->Problems() : 0;
	m_audioProblems = m_pAudio ? m_pAudio->Problems() : 0;
	Shutdown();
}


void Application::RunCommandLine(const std::string &line)
{
	const std::size_t before = m_console.Lines().size();
	m_console.Execute(line);
	const std::vector<DevTools::ConsoleLine> lines = m_console.Lines();
	for (std::size_t index = before; index < lines.size(); ++index)
		if (lines[index].kind == DevTools::ConsoleLine::Kind::Error)
			throw std::runtime_error(std::format("--run '{}': {}", line, lines[index].text));
}


void Application::Tick()
{
	JETTISON_PROFILE_FUNCTION();
	const bool loading = m_pResources->Loading() != 0;
	++m_frameIndex;
	for (auto command = m_pendingCommands.begin(); command != m_pendingCommands.end();)
	{
		const bool due = command->simulationSeconds ? m_sim.Clock().Now() >= *command->simulationSeconds
		                                            : m_frameIndex >= command->frame;
		if (!due)
		{
			++command;
			continue;
		}
		const std::string line = command->line;
		command = m_pendingCommands.erase(command);
		RunCommandLine(line);
	}
	double deltaTime = 0.0;
	if (m_arguments.frameStep > 0.0)
	{
		// The clocks held still while anything streams in, so what a frame shows is a function of how
		// many frames have been drawn since the scene had all it asked for, however long either took.
		deltaTime = loading ? 0.0 : m_arguments.frameStep;
		m_fixedTime += deltaTime;
		m_frameStart = m_loopStart + std::chrono::duration_cast<Time::RealClock::Clock::duration>(
		                                 std::chrono::duration<double>{m_fixedTime});
	}
	else
	{
		m_frameStart = Time::RealClock::Clock::now();
		deltaTime = m_frameClock.Tick(m_frameStart).count();
	}
	// The screenshot the run was asked for, of the frame after a run of them with nothing loading;
	// the run ends with it, and the renderer writes it as it is destroyed.
	m_quietFrames = loading ? 0 : m_quietFrames + 1;
	const bool shoot = !m_arguments.screenshotPath.empty() && !m_screenshotTaken &&
	                   m_quietFrames > m_arguments.screenshotAfterFrames;
	if (shoot)
		m_pRenderer->RequestScreenshot(m_arguments.screenshotPath);
	for (const Ecs::TickPhase phase : Ecs::kTickPhases)
		m_frame.Run(phase, m_pScene->Registry(), deltaTime);
	// Taken once a frame has drawn it; a frame that drew nothing leaves the request for the next.
	if (shoot && !m_pRenderer->ScreenshotRequested())
	{
		m_screenshotTaken = true;
		RequestClose();
		// Where the run's clocks were when the frame was taken, so two captures that differ can be
		// told apart by their timelines before their pixels.
		std::string sequences;
		for (const auto [camera, player] :
		     m_pScene->Registry().view<const Scene::SequencePlayerComponent>().each())
			sequences += std::format(", {} at {:.6f} s", player.file, player.time);
		Core::Log(
		    Core::Severity::Info, kShell, "Screenshot at frame {}, fixed time {:.6f} s{}", m_frameIndex,
		    m_fixedTime, sequences);
	}
	JETTISON_PROFILE_FRAME();
}


void Application::DrawUI()
{
	// The theme is applied here and not at start-up, because it wants the attachment the UI draws
	// into: on an sRGB swapchain its palette goes to that attachment in linear light, so a hex reads
	// as the palette writes it (devtools/Theme.h). One call a frame, and it does nothing when nothing
	// has changed, which is how `dev.palette mocha` takes effect on the frame after it is typed.
	DevTools::ApplyThemeIfChanged(m_theme, m_pRenderer->UiColoursLinearised());
	m_pRenderer->BeginUi();
	if (m_showUI)
	{
		DevTools::DrawDevTools(m_console, m_devTools, m_theme, &m_showUI);
		DrawRendererPanel();
		DrawScenePanel();
	}
	else if (m_devTools.showConsole)
		DevTools::DrawConsole(m_console, m_devTools.console, m_theme, &m_devTools.showConsole);
	DrawPicked();
	m_pGame->DrawPanels(*this, m_showUI);
	m_pRenderer->EndUi();
}


void Application::DrawScenePanel()
{
	if (!ImGui::Begin("Scene"))
	{
		ImGui::End();
		return;
	}
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
	ImGui::End();
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
	    "Meshes %u: %u outside the view, %u behind a horizon, %u too small to see", stats.meshes,
	    stats.frustumCulled, stats.horizonCulled, stats.detailCulled);
	ImGui::Text(
	    "View: %u draws, %llu triangles", stats.viewBatches,
	    static_cast<unsigned long long>(stats.viewTriangles));
	ImGui::Text(
	    "Shadow casters %u / %u / %u / %u, %llu triangles", stats.shadowCasters[0],
	    stats.shadowCasters[1], stats.shadowCasters[2], stats.shadowCasters[3],
	    static_cast<unsigned long long>(stats.shadowTriangles));
	ImGui::Text("Lights %u, %u listed", stats.lights, stats.listedLights);
	ImGui::Text(
	    "Transparent: %u draws; %u emitters in %u particle slots", stats.transparentBatches,
	    stats.emitters, stats.particleSlots);
	ImGui::Text("Outlines %u", stats.outlines);
	bool prepass = m_pRenderer->GetDepthPrepass();
	if (ImGui::Checkbox("Depth prepass", &prepass))
		m_pRenderer->SetDepthPrepass(prepass);
	ImGui::SameLine();
	bool occlusion = m_pRenderer->GetAmbientOcclusion();
	if (ImGui::Checkbox("Ambient occlusion", &occlusion))
		m_pRenderer->SetAmbientOcclusion(occlusion);
	float bloom = m_pRenderer->GetBloom();
	if (ImGui::SliderFloat("Bloom", &bloom, 0.0f, 0.2f, "%.3f"))
		m_pRenderer->SetBloom(bloom);
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
		passes += std::format("  {:<24} {:8.3f} ms\n", timing.name, timing.meanMs);
	const Render::FrameStats &stats = m_frameStats;
	const double frames =
	    m_measuredFrames == 0 ? 1.0 : static_cast<double>(m_arguments.benchmarkFrames);
	Core::Log(
	    Core::Severity::Info, kShell,
	    "Benchmark: {} on {} at {}x{}, {} frames after {} to warm up, {} GPU frames measured, depth "
	    "prepass {}, ambient occlusion {}, bloom {}\n"
	    "GPU time per pass (mean):\n{}"
	    "Wall time per frame: {:.3f} ms\n"
	    "Meshes {}: {} outside the view, {} behind a horizon, {} too small to see; {} draws of {} "
	    "triangles\n"
	    "Shadow casters {} / {} / {} / {} in {} / {} / {} / {} draws, {} triangles\n"
	    "Lights {}, {} listed\n"
	    "Transparent: {} draws; {} emitters in {} particle slots\n"
	    "Outlines {}",
	    !m_arguments.systemPath.empty() ? m_arguments.systemPath
	    : m_arguments.scenePath.empty() ? std::string{m_pGame->DefaultScene()}
	                                    : m_arguments.scenePath.filename().string(),
	    m_deviceName, m_frameWidth, m_frameHeight, m_arguments.benchmarkFrames, kBenchmarkWarmUp,
	    m_measuredFrames, m_arguments.depthPrepass ? "on" : "off",
	    m_benchmarkAmbientOcclusion ? "on" : "off", m_arguments.bloom, passes,
	    m_benchmarkSeconds * 1000.0 / frames, stats.meshes, stats.frustumCulled, stats.horizonCulled,
	    stats.detailCulled, stats.viewBatches, stats.viewTriangles, stats.shadowCasters[0],
	    stats.shadowCasters[1], stats.shadowCasters[2], stats.shadowCasters[3], stats.shadowBatches[0],
	    stats.shadowBatches[1], stats.shadowBatches[2], stats.shadowBatches[3], stats.shadowTriangles,
	    stats.lights, stats.listedLights, stats.transparentBatches, stats.emitters, stats.particleSlots,
	    stats.outlines);
}


void Application::PollInput()
{
	// The game UI first, so whether it has the mouse is this frame's answer: a click on a screen's
	// button is not a pick, and the wheel over it does not zoom the camera.
	FeedGameUi();
	const bool uiMouse = (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse) ||
	                     (m_pUi && m_pUi->WantsMouse());
	m_glfwInput.Feed(m_pRenderer->GetGLFWWindow(), m_input, uiMouse);
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
	if (m_input.WasPressedThisFrame(m_toggleAmbientAccess))
		toggleView(Render::DebugView::AmbientAccess);
	if (m_input.WasPressedThisFrame(m_cameraNext))
		NextCameraTarget(m_pScene->Registry());
	if (m_input.WasPressedThisFrame(m_quickSave))
		SaveTo("quicksave");
	if (m_input.WasPressedThisFrame(m_quickLoad))
		LoadFrom("quicksave");
	if (m_input.WasPressedThisFrame(m_toggleSaves) && !(m_pUi && m_pUi->WantsKeyboard()))
		ShowSaves(!m_showSaves);
	m_pGame->PollInput(m_input, *this);
	// A click on the scene, not on a window over it, ImGui's or the game UI's.
	if (m_input.WasPressedThisFrame(m_pickAction) && !uiMouse)
		m_clicked = m_glfwInput.Cursor();
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
		else if (m_showSaves)
			ShowSaves(false);
		else if (m_pGame->Back(*this)) {}
		else if (!m_pScene->Registry().view<Scene::SequencePlayerComponent>().empty())
			StopSequence();
		else
			RequestClose();
	}
	SyncContexts();
}


void Application::LoadScene()
{
	// The outlined proxy goes with the scene's old contents: its number may be another entity's in
	// the new.
	m_outlined = entt::null;
	if (!m_arguments.systemPath.empty())
		m_pScene->LoadSystem(m_arguments.systemPath);
	else if (!m_arguments.scenePath.empty())
		m_pScene->Load(m_arguments.scenePath);
	else
	{
		// The content's own scene, through the mounts, so a packed build and a mod's scene are read
		// as a directory's; without one, as the engine with no content runs, the scene is empty.
		const std::string_view scene = m_pGame->DefaultScene();
		const auto bytes = m_assets.Read(scene);
		if (!bytes)
		{
			Core::Log(
			    Core::Severity::Info, kShell, "The content has no {}, so the scene is empty", scene);
			m_pScene->Clear();
		}
		else
		{
			try
			{
				m_pScene->LoadText({reinterpret_cast<const char *>(bytes->data()), bytes->size()});
			}
			catch (const std::exception &error)
			{
				throw std::runtime_error(std::format("Scene '{}': {}", scene, error.what()));
			}
		}
	}
	m_simStepper.Resync();
	m_pGame->SceneLoaded(*this);
}


void Application::ReloadScene()
{
	m_pGame->SceneUnloading(*this);
	LoadScene();
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
	std::vector<entt::entity> targets;
	for (const entt::entity entity : registry.view<const OrbitTarget>())
		targets.push_back(entity);
	if (targets.empty())
		return;
	std::ranges::sort(targets);
	for (const auto [entity, orbit] : registry.view<const Scene::OrbitCameraComponent>().each())
	{
		const auto current = std::ranges::find(targets, orbit.target);
		FrameTarget(
		    registry, entity,
		    current == targets.end() || std::next(current) == targets.end() ? targets.front()
		                                                                    : *std::next(current));
	}
}


void Application::PlaceOrbitCamera(std::span<const std::string> arguments)
{
	if (arguments.size() < 3)
		throw std::invalid_argument{"takes a yaw, a pitch and a distance"};
	std::array<double, 3> values{};
	for (std::size_t index = 0; index < values.size(); ++index)
	{
		const std::string &text = arguments[index];
		const auto [end, error] =
		    std::from_chars(text.data(), text.data() + text.size(), values[index]);
		if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(values[index]))
			throw std::invalid_argument{std::format("'{}' is not a number", text)};
	}
	if (!(values[2] > 0.0))
		throw std::invalid_argument{"the distance has to be more than zero"};
	std::string name;
	for (const std::string &word : arguments.subspan(3))
		name += (name.empty() ? "" : " ") + word;

	entt::registry &registry = m_pScene->Registry();
	entt::entity body = entt::null;
	if (!name.empty())
	{
		for (const auto [entity, target] : registry.view<const OrbitTarget>().each())
			if (target.name == name)
				body = entity;
		if (body == entt::null)
			throw std::invalid_argument{std::format("there is no target '{}' in the scene", name)};
	}
	bool placed = false;
	for (const auto [camera, orbit] : registry.view<Scene::OrbitCameraComponent>().each())
	{
		if (body != entt::null)
			orbit.target = body;
		// Placed exactly where it is asked, not eased there, and allowed in as far as it was asked:
		// a frame is placed to be captured.
		orbit.yaw = values[0];
		orbit.pitch = values[1];
		orbit.distance = values[2];
		orbit.minDistance = std::min(orbit.minDistance, values[2]);
		orbit.transition = {};
		orbit.placed = false;
		placed = true;
	}
	if (!placed)
		throw std::invalid_argument{"the scene has no orbit camera"};
}


void Application::PlaySequence(std::string_view name, double from)
{
	entt::registry &registry = m_pScene->Registry();
	entt::entity camera = entt::null;
	for (const auto [entity, component] : registry.view<const Scene::CameraComponent>().each())
		if (component.active)
		{
			camera = entity;
			break;
		}
	if (camera == entt::null)
	{
		Core::Log(Core::Severity::Warning, kShell, "No camera to play sequence '{}' on", name);
		return;
	}
	Scene::SequencePlayerComponent player;
	player.file = std::format("cinematics/{}.sequence.yaml", name);
	player.time = from;
	registry.emplace_or_replace<Scene::SequencePlayerComponent>(camera, std::move(player));
	Core::Log(Core::Severity::Info, kShell, "Playing sequence '{}' from {:.1f} s", name, from);
}


void Application::StopSequence()
{
	m_pScene->Registry().clear<Scene::SequencePlayerComponent>();
	m_pRenderer->SetLetterbox(0.0f);
}


void Application::ShowSequence(entt::registry &registry)
{
	const auto players = registry.view<const Scene::SequencePlayerComponent>();
	const Scene::SequencePlayerComponent *player =
	    players.empty() ? nullptr : &players.get<const Scene::SequencePlayerComponent>(players.front());
	m_pRenderer->SetLetterbox(player != nullptr ? player->letterbox : 0.0f);
	// A game's fade over it, a jump's, is set after this. The renderer keeps a fade until it is set
	// again, so the sequence's is cleared the frame it ends, and otherwise left to the game's.
	const float fade = player != nullptr ? player->fade : 0.0f;
	if (fade > 0.0f || m_sequenceFaded)
		m_pRenderer->SetFade(player != nullptr ? player->fadeColor : glm::vec3{0.0f}, fade);
	m_sequenceFaded = fade > 0.0f;
}


void Application::SetLanguage(DevTools::Console &console, std::span<const std::string> arguments)
{
	if (arguments.size() > 1)
		throw std::invalid_argument{"takes a language, as de or pt-BR"};
	if (arguments.empty())
	{
		std::string languages;
		for (const std::string &language : Ui::StringTable::Languages(m_assets))
			languages += (languages.empty() ? "" : ", ") + language;
		console.Print(
		    std::format(
		        "The screens are in '{}'; there are tables for {}", m_pUi->Strings().Language(),
		        languages));
		return;
	}
	m_pUi->SetLanguage(arguments[0]);
	m_settings->Values().Set(Core::ConfigLayer::User, kLanguageKey, arguments[0]);
	SaveSettings();
	console.Print(std::format("The screens are in '{}'", arguments[0]));
}


void Application::SetGrade(std::span<const std::string> arguments)
{
	if (arguments.empty() || arguments.size() > 2)
		throw std::invalid_argument{"takes a table's name and a strength, or off"};
	entt::registry &registry = m_pScene->Registry();
	const auto graded = registry.view<Scene::ColourGradeComponent>();
	if (arguments[0] == "off")
	{
		registry.clear<Scene::ColourGradeComponent>();
		return;
	}
	float strength = 1.0f;
	if (arguments.size() == 2)
	{
		const std::string &text = arguments[1];
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), strength);
		if (error != std::errc{} || end != text.data() + text.size() || !(strength >= 0.0f) ||
		    !(strength <= 1.0f))
			throw std::invalid_argument{std::format("'{}' is not a strength from 0 to 1", text)};
	}
	// A name is a table in grades/; a path is taken as it is.
	const std::string &name = arguments[0];
	Scene::ColourGradeComponent grade{
	    name.find('/') != std::string::npos ? name : std::format("grades/{}.cube", name), strength};
	if (!graded.empty())
	{
		registry.replace<Scene::ColourGradeComponent>(graded.front(), std::move(grade));
		return;
	}
	// The scene's own grade, on its active camera, which a scene saved from the Scene panel keeps.
	for (const auto [entity, camera] : registry.view<const Scene::CameraComponent>().each())
		if (camera.active)
		{
			registry.emplace<Scene::ColourGradeComponent>(entity, std::move(grade));
			return;
		}
	throw std::invalid_argument{"the scene has no active camera to grade"};
}


void Application::FrameTarget(entt::registry &registry, entt::entity camera, entt::entity target)
{
	// Out to three of the target's radii, eased over two seconds from wherever the camera is.
	double radius = 50.0;
	if (const auto *marked = registry.try_get<OrbitTarget>(target))
		radius = std::max(radius, marked->radius);
	auto &orbit = registry.get<Scene::OrbitCameraComponent>(camera);
	if (const auto *pose = registry.try_get<Scene::CameraPoseComponent>(camera))
		orbit.transition =
		    Scene::CameraTransition{{pose->position, pose->orientation}, 2.0, Math::Easing::InOutCubic};
	orbit.target = target;
	orbit.minDistance = radius * 1.1;
	orbit.distance = radius * 3.0;
	orbit.placed = false;
}


void Application::SyncContexts()
{
	if (m_devTools.showConsole == m_consoleContext && m_gameContext == m_pushedGameContext)
		return;
	// Down to the shell's, then up again in order, so the console is always on top.
	if (m_consoleContext)
		m_input.PopContext();
	if (!m_pushedGameContext.empty())
		m_input.PopContext();
	if (!m_gameContext.empty())
		m_input.PushContext(m_gameContext);
	if (m_devTools.showConsole)
		m_input.PushContext("console");
	m_pushedGameContext = m_gameContext;
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
		save.info.mods = m_mods;
		save.AddSection(std::string{Script::kStoreSection}, Script::WriteStore(m_pScripts->Store()));
		m_pGame->WriteSave(save, *this);
		const std::vector<std::byte> bytes = save.Write();
		m_assets.Write(path, bytes);
		if (m_showSaves)
			ShowSaves(true);
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
		// Everything read before the world is replaced, so a save that does not fit changes nothing.
		// A save from before scripts kept keys holds none.
		const Sim::SaveSection *keys = save.FindSection(Script::kStoreSection);
		Script::ScriptStore store = keys ? Script::ReadStore(keys->data) : Script::ScriptStore{};
		m_pGame->ReadSave(save, *this);
		m_sim.Load(save.world);
		m_pGame->Loaded(*this);
		m_pScripts->SetStore(std::move(store));
		// Other mods may make another game of the save, which is the player's to judge: it loads.
		for (const std::string &difference : Sim::ModDifferences(save.info.mods, m_mods))
			Core::Log(
			    Core::Severity::Warning, kShell, "'{}' was saved with other mods: {}", name,
			    difference);

		ClearPick();
		m_simStepper.Resync();
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
		ReloadScene();
		m_pResources->ReloadAll();
	};
	// The pilot's and the jump's stages are a game's to drive (DriveSmoke): the shell has nothing
	// to fly and nowhere to jump.
	actions.pilot = [](bool) { return false; };
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
	// The game UI's stage is the game's (DriveSmoke): the shell has no screen of its own to drive.
	actions.gameUi = [] { return std::optional<bool>{}; };
	actions.gameUiWaiting = [] { return false; };
	actions.gameUiAnswered = [] { return true; };
	actions.close = [this] { RequestClose(); };
	m_pGame->DriveSmoke(actions, *this);
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
		// The window's callbacks go before the window does.
		m_glfwInput.Detach();
		// The UI holds no device object: the renderer's copies of its textures go with the renderer.
		if (m_pAudio)
			m_soundSources.Clear(*m_pAudio);
		m_eventSounds.reset();
		m_pAudio.reset();
		m_pSavesModel = nullptr;
		m_pUi.reset();
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
	// What the game holds of the scene and the resources goes first. The scene's rigs are resource
	// handles, released as it goes; then the manager, which holds no GPU object once the renderer
	// has released its handles, goes before the worker it posts to.
	m_pGame->ShuttingDown(*this);
	m_pScene.reset();
	m_sceneUpdateSystem.reset();
	m_sequences.reset();
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


bool Application::Pick(const glm::dvec2 &point)
{
	const std::optional<PickSpace> space = m_pGame->PickIn(*this);
	if (!space)
		return false;
	int width = 0;
	int height = 0;
	glfwGetWindowSize(m_pRenderer->GetGLFWWindow(), &width, &height);
	if (width <= 0 || height <= 0)
		return false;
	// The camera as it was drawn last frame, which is what was clicked on, and the time it showed.
	const Math::DRay ray = Render::ScreenRay(
	    Render::ActiveCameraView(m_pScene->Registry()), point, glm::dvec2{width, height});
	m_pickTime = m_shownTime;
	m_sim.Commands().Push(
	    Physics::Pick{
	        kPilot, ++m_pickRequest, space->frame, ray.origin, ray.direction, kPickDistance, m_pickTime,
	        space->ignore});
	m_pickPending = true;
	return true;
}


void Application::TakeEvents()
{
	for (const Sim::SimEvent &event : m_sim.Events().Take())
	{
		m_pScripts->Dispatch(event);
		PlayEventSound(event);
		if (const auto *picked = event.event.try_cast<Physics::Picked>())
		{
			// Another player's, or an answer to a pick since superseded, is not what the mouse last
			// asked about.
			if (picked->sender != kPilot || picked->request != m_pickRequest)
				continue;
			m_pickPending = false;
			if (picked->entity == entt::null)
			{
				m_picked.reset();
				Core::Log(Core::Severity::Info, kShell, "Picked nothing");
				continue;
			}
			m_picked = PickedMark{
			    picked->entity, picked->distance,
			    picked->point - m_pGame->PickOrigin(*this, m_pickTime), picked->normal};
			Core::Log(
			    Core::Severity::Info, kShell, "Picked {} at {:.0f} m", Describe(picked->entity),
			    picked->distance);
		}
		m_pGame->TakeEvent(event, *this);
	}
}


void Application::MarkScene(entt::registry &registry)
{
	// The outline the shell placed last frame comes off, and this frame's goes on: the proxies of the
	// simulation's entities come and go with its snapshots, so it is placed on what is there now. An
	// outline a scene file authored is its own, and stays.
	if (m_outlined != entt::null && registry.valid(m_outlined))
		registry.remove<Scene::OutlineComponent>(m_outlined);
	m_outlined = entt::null;
	if (!m_picked)
		return;
	entt::entity proxy = entt::null;
	for (const auto [entity, sim] : registry.view<const Scene::SimProxyComponent>().each())
		if (sim.simEntity == m_picked->entity)
			proxy = entity;
	if (proxy == entt::null || registry.all_of<Scene::OutlineComponent>(proxy))
		return;
	// What was picked is selected, a line about it; as the game's target, a glow.
	Scene::OutlineComponent outline;
	if (const std::optional<PickSpace> space = m_pGame->PickIn(*this); space && space->targeting)
	{
		outline.style = Scene::OutlineStyle::Targeted;
		outline.color = {1.0f, 0.2f, 0.1f, 2.0f};
		outline.width = 14.0f;
	}
	registry.emplace<Scene::OutlineComponent>(proxy, outline);
	m_outlined = proxy;
}


void Application::DrawPicked()
{
	if (!m_picked)
		return;
	// Where it was hit, carried with its frame (Game::PickOrigin), and the surface's normal there; a
	// marker that keeps its size on the screen, about a hundredth of the distance across.
	const glm::dvec3 at = m_pGame->PickOrigin(*this, m_shownTime) + m_picked->local;
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


std::string Application::Word(const Sim::Reason &reason) const
{
	return WordReason(reason, m_simStepper.Current(), *m_definitions, m_pUi->Strings(), m_pGame);
}


void Application::ClearPick()
{
	// Its outline off now, as a game clears it before it replaces the scene.
	if (m_outlined != entt::null && m_pScene && m_pScene->Registry().valid(m_outlined))
		m_pScene->Registry().remove<Scene::OutlineComponent>(m_outlined);
	m_outlined = entt::null;
	m_picked.reset();
	m_pickPending = false;
}


std::string Application::Describe(entt::entity simEntity) const
{
	if (std::optional<std::string> name = m_pGame->Describe(simEntity))
		return *std::move(name);
	return std::format("Entity {:#x}", entt::to_integral(simEntity));
}


const AppArguments &Application::Arguments() const
{
	return m_arguments;
}


Sim::SimWorld &Application::World()
{
	return m_sim;
}


void Application::Resync()
{
	m_simStepper.Resync();
}


Scene::Scene &Application::Scene()
{
	JETTISON_ASSERT(m_pScene);
	return *m_pScene;
}


const Sim::Snapshot &Application::Snapshot() const
{
	return m_simStepper.Current();
}


const Sim::Snapshot &Application::Previous() const
{
	return m_simStepper.Previous();
}


double Application::Alpha() const
{
	return m_simStepper.Alpha();
}


entt::entity Application::ActiveCamera() const
{
	for (const auto [entity, camera] : m_pScene->Registry().view<const Scene::CameraComponent>().each())
		if (camera.active)
			return entity;
	return entt::null;
}


Render::DebugDraw &Application::DebugLines()
{
	return m_debugDraw;
}


bool Application::DebugShown() const
{
	return m_showSceneDebug;
}


void Application::SetFade(const glm::vec3 &colour, float amount)
{
	if (m_pRenderer)
		m_pRenderer->SetFade(colour, amount);
}


Vfs::FileSystem &Application::Assets()
{
	return m_assets;
}


Resource::ResourceManager &Application::Resources()
{
	JETTISON_ASSERT(m_pResources);
	return *m_pResources;
}


DevTools::Console &Application::Console()
{
	return m_console;
}


Input::InputSystem &Application::Input()
{
	return m_input;
}


void Application::SetGameContext(std::string_view context)
{
	m_gameContext = context;
	SyncContexts();
}


const Sim::DefinitionRegistry &Application::Definitions() const
{
	return *m_definitions;
}


Script::ScriptHost &Application::Scripts()
{
	return *m_pScripts;
}


Sim::Controller Application::Player() const
{
	return kPilot;
}


void Application::Frame(entt::entity target)
{
	entt::registry &registry = m_pScene->Registry();
	for (const entt::entity camera : registry.view<const Scene::OrbitCameraComponent>())
		FrameTarget(registry, camera, target);
}


Ui::UiSystem *Application::GameUi()
{
	return m_pUi.get();
}


bool Application::SmokeTest() const
{
	return m_arguments.smokeTest;
}


ShellWorld Application::MakeShellWorld()
{
	return {m_sim, *m_pPhysics};
}
} // namespace Jettison::App
