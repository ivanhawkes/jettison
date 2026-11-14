#include "Application.h"

// The window's input and size; the renderer behind IRenderer owns the window itself.
#include <GLFW/glfw3.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
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

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Cruise.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Routes.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/UtilityAi.h>
#include <jettison/animation/ClipFile.h>
#include <jettison/animation/Components.h>
#include <jettison/animation/RigLoader.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>
#include <jettison/core/Version.h>
#include <jettison/dock/Docks.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/math/Random.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/platform/Crash.h>
#include <jettison/profile/Profile.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/render/Camera.h>
#include <jettison/render/IRenderer.h>
#include <jettison/render/Overlay.h>
#include <jettison/render/SceneView.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/scene/SystemScene.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/SystemIndices.h>

#include "Dressing.h"
#include "Mounts.h"
#include "Reasons.h"
#include "Saves.h"
#include "Stations.h"
#include "Traders.h"
#include "Trading.h"

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
// And the flash it passes through on the way, rising and lifting: a blue-white over the frame at
// half cover, darkening to black as it covers the rest.
constexpr glm::vec3 kJumpFlash{0.72f, 0.84f, 1.0f};
// The streaks a jump's effect draws past the camera as it rises and lifts: born within a sphere this
// far ahead and this wide, at this speed towards it.
constexpr double kStreaksAhead = 80.0;
constexpr float kStreaksRadius = 30.0f;
constexpr float kStreaksSpeed = 320.0f;
constexpr Core::Category kShell{"shell"};
// The scene a run of no --scene and no --system loads, by virtual path.
constexpr std::string_view kDefaultScene = "scene/main.yaml";

// The shell's ship: a shuttle about ten metres long, as its proxy draws it, whose hull
// (defs/hulls/shuttle.yaml) is the proxy's bounds. At a station of no pads it holds 150 m off the
// station's hull, nose towards it. Its pilot plays in the first slot, and its commands say so.
constexpr std::string_view kShipHull = "shuttle";
constexpr float kShipMass = 20000.0f;
constexpr double kShipStandOff = 150.0;
constexpr Sim::Controller kPilot{Sim::ControllerKind::Player, 0};

// The most traffic the console sends to a station, and how fast it flies: a shuttle's full 150 m/s
// crosses the ring's 2 km in a quarter of a minute, too fast to watch a ship give way.
constexpr std::size_t kMostTraffic = 16;
constexpr float kTrafficSpeed = 80.0f;

// A place on the traffic's ring about a station, in its axes: about its Z, the axis its dock is
// at the end of, so the ship holding off the dock is clear of every one.
glm::dvec3 TrafficPlace(std::size_t place, double ring)
{
	const double angle = glm::two_pi<double>() * static_cast<double>(place) / kMostTraffic;
	return {ring * std::cos(angle), ring * std::sin(angle), 0.0};
}
// How far a pick reaches: past any station a ship is flown by.
constexpr double kPickDistance = 50000.0;
// How long the camera eases into and out of the ship's rigs, and between them.
constexpr double kPilotTransition = 1.5;
constexpr double kViewTransition = 0.5;
// The cinematic the ship landing on a pad plays (cinematics/dock.sequence.yaml).
constexpr std::string_view kDockSequence = "dock";
// The shell's ship as the sample game starts it: a small hauler's hold, and the credits to fill most
// of it with what the station it starts at has to spare (docs/sample-game.md).
constexpr std::int64_t kShipHold = 20;
constexpr std::int64_t kStartingCredits = 250000;
// The game UI's faces: Roboto for text, Hack for figures, and Droid Sans for a glyph neither has.
constexpr std::array<std::pair<std::string_view, bool>, 3> kUiFonts{{
    {"fonts/Roboto-Medium.ttf", false},
    {"fonts/Hack-Regular.ttf", false},
    {"fonts/DroidSans.ttf", true},
}};
// In the smoke run nothing presses a key: the ship flies ahead at a fifth of its speed.
// The shell's music, and the hum of the station the ship is bound to: heard at its full volume within
// a few hundred metres of the station's centre, as a station is that size, and fading to a murmur
// two kilometres out.
constexpr std::string_view kMusic = "audio/music/drift.flac";
constexpr std::string_view kStationHum = "audio/effects/station-hum.wav";
constexpr Audio::Falloff kStationHumFalloff{300.0f, 20000.0f, 1.0f};
constexpr float kStationHumVolume = 0.6f;

// The language the screens are in (StringTable), in the settings.
constexpr std::string_view kLanguageKey = "ui.language";

// A bus's volume in the settings: audio.master, audio.music, ...
std::string VolumeKey(Audio::Bus bus)
{
	return std::format("audio.{}", Audio::BusName(bus));
}

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

Application::Application(AppArguments arguments, std::string windowTitle)
    : m_arguments{std::move(arguments)}, m_windowTitle{std::move(windowTitle)}
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
	m_jumpNext = m_input.DefineAction("jump_next", Input::ActionType::Button);
	m_pilot = m_input.DefineAction("pilot", Input::ActionType::Button);
	m_pickAction = m_input.DefineAction("pick", Input::ActionType::Button);
	m_toggleMarket = m_input.DefineAction("toggle_market", Input::ActionType::Button);
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
	m_input.Bind("shell", m_jumpNext, {Character('j')});
	m_input.Bind("shell", m_cameraWheel, {{Input::Device::MouseAxis, 3}});
	m_input.Bind("shell", m_pilot, {Character('p')});
	m_input.Bind("shell", m_pilot, {{Input::Device::GamepadButton, 7}});
	m_input.Bind("shell", m_pickAction, {{Input::Device::MouseButton, 0}});
	m_input.Bind("shell", m_toggleMarket, {Character('m')});
	m_input.Bind("shell", m_toggleSaves, {Character('l')});
	m_input.Bind("shell", m_quickSave, {Input::Keys::F5});
	m_input.Bind("shell", m_quickLoad, {Input::Keys::F9});
	m_pilotInput.emplace(m_input);
	m_input.PushContext("shell");

	m_console.CaptureLog();
	m_console.RegisterCommand(
	    "quit", "Closes the window and ends the run",
	    [this](DevTools::Console &, std::span<const std::string>) { RequestClose(); });
	m_console.RegisterCommand(
	    "jump",
	    "Jumps to the star system named, or out through the next lane, taking the ship along a "
	    "lane as a tool, past the gate and its toll: jump [system]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::string name;
		    for (const std::string &word : arguments)
			    name += (name.empty() ? "" : " ") + word;
		    Jump(name);
	    });
	m_console.RegisterCommand(
	    "cruise", "Sends the ship on a cruise to the body of its system named: cruise <body>",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::string name;
		    for (const std::string &word : arguments)
			    name += (name.empty() ? "" : " ") + word;
		    if (!CanPilot())
			    Core::Log(Core::Severity::Warning, kShell, "No cruise: no ship in the system shown");
		    else
			    m_sim.Commands().Push(Ai::Cruise{kPilot, m_ship, name});
	    });
	m_console.RegisterCommand(
	    "transit",
	    "Asks a gate of the ship's system to clear it through, the one whose frame it is in unless "
	    "another is named; flown through the ring, the gate sends it and charges the lane's toll: "
	    "transit [gate]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::string name;
		    for (const std::string &word : arguments)
			    name += (name.empty() ? "" : " ") + word;
		    RequestTransit(name);
	    });
	m_console.RegisterCommand(
	    "route", "Says the quickest way from the ship to the star system named: route <system>",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::string name;
		    for (const std::string &word : arguments)
			    name += (name.empty() ? "" : " ") + word;
		    LogRoute(name);
	    });

	m_console.RegisterCommand(
	    "traffic",
	    "Sends shuttles to hold station about the ship's station, or takes them all away: traffic "
	    "[count], 6 by default, 0 for none",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    int count = 6;
		    if (!arguments.empty())
		    {
			    const auto [end, error] = std::from_chars(
			        arguments[0].data(), arguments[0].data() + arguments[0].size(), count);
			    if (error != std::errc{} || end != arguments[0].data() + arguments[0].size() ||
			        count < 0)
			    {
				    Core::Log(
				        Core::Severity::Warning, kShell, "traffic takes a count, not '{}'",
				        arguments[0]);
				    return;
			    }
		    }
		    SpawnTraffic(count);
	    });

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
	    "traders", "Says what each trader is doing, and what its runs have made",
	    [this](DevTools::Console &, std::span<const std::string>) { LogTraders(); });
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
	    "dock",
	    "Asks the ship's station for a pad and flies the ship onto it, or leaves the landing to the "
	    "pilot: dock [manual]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { RequestPad(arguments.empty() || arguments[0] != "manual"); });
	m_console.RegisterCommand(
	    "pilot",
	    "Flies the ship from behind it or from its cockpit, or stops: pilot [chase|cockpit|off], "
	    "chase by default",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    const std::string view = arguments.empty() ? "chase" : arguments[0];
		    if (view == "off")
			    StopPiloting();
		    else if (view == "chase" || view == "cockpit")
			    StartPiloting(view == "chase" ? Scene::PilotView::Chase : Scene::PilotView::Cockpit);
		    else
			    Core::Log(
			        Core::Severity::Warning, kShell, "pilot takes chase, cockpit or off, not '{}'",
			        view);
	    });
	m_console.RegisterCommand(
	    "undock", "Lifts the ship clear of its pad and gives the pad up, or gives up one not landed on",
	    [this](DevTools::Console &, std::span<const std::string>) { ReleasePad(); });
	m_console.RegisterCommand(
	    "trade",
	    "Buys crates of a good at the market of the system shown, or sells them with a negative "
	    "count, as the market screen does: trade <good> <crates>",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::int64_t crates = 0;
		    const auto [end, error] =
		        arguments.size() == 2
		            ? std::from_chars(
		                  arguments[1].data(), arguments[1].data() + arguments[1].size(), crates)
		            : std::from_chars_result{nullptr, std::errc::invalid_argument};
		    if (error != std::errc{} || end != arguments[1].data() + arguments[1].size() || crates == 0)
		    {
			    Core::Log(Core::Severity::Warning, kShell, "trade takes a good and a count of crates");
			    return;
		    }
		    SendTrade(arguments[0], crates);
	    });
	m_console.RegisterCommand(
	    "market", "Shows or hides the market screen: market [on|off]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { ShowMarket(arguments.empty() ? !m_showMarket : arguments[0] != "off"); });
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
	    "Places the orbit camera at once, in radians and metres, about the celestial body named or "
	    "the one it is on: orbit <yaw> <pitch> <distance> [body]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { PlaceOrbitCamera(arguments); });

	Sim::RegisterTypes();
	m_sim.RegisterComponent<Sim::Transform>();
	m_sim.RegisterComponent<Sim::InSystem>();
	m_pPhysics = &Physics::PhysicsWorld::Attach(m_sim);
	m_pLanes = &Ai::LaneTravel::Attach(m_sim, *m_pPhysics);
	m_pGates = &Ai::Gates::Attach(m_sim, *m_pPhysics, *m_pLanes);
	m_pCruises = &Ai::CruiseTravel::Attach(m_sim, *m_pPhysics);
	m_pIndices = &Spatial::SystemIndices::Attach(m_sim);
	m_pSteering = &Ai::Steering::Attach(m_sim, *m_pPhysics, *m_pIndices);
	m_pDocks = &Dock::Docks::Attach(m_sim, *m_pPhysics);
	m_pBehaviours = &Ai::Behaviours::Attach(m_sim);
	m_sim.RegisterComponent<Sim::ControlledBy>();
	// For what a reason says of a station or a gate, by its body's name (Reasons.h).
	m_sim.ShowInSnapshots<Physics::OnRails>();
	// For the frame the ship flies in, which a cruise changes (ShipFrame).
	m_sim.ShowInSnapshots<Physics::LocalFrame>();
	RegisterTrading(m_sim);
	ChargeTolls(*m_pGates);
	RegisterMarketScreenTypes();
	RegisterSavesScreenTypes();
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
		     const float fade = m_jump->Fade();
		     m_pRenderer->SetFade(
		         glm::mix(kJumpFlash, kJumpColor, glm::smoothstep(0.4f, 1.0f, fade)), fade);
		     ShowJumpStreaks();
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
	// The stations' lamps, from the same snapshot the traffic layer drew.
	m_frame.Register(
	    {"shell.pads",
	     TickPhase::Sync,
	     Access::Of<>().Writing<Scene::MeshComponent>(),
	     {"sim.sync"},
	     [this](entt::registry &, double) { ShowPadStates(); }});
	// What each buggy carries, from the same snapshot.
	m_frame.Register(
	    {"shell.buggies",
	     TickPhase::Sync,
	     Access::Of<BuggyLoad>().Writing<TransformComponent>(),
	     {"sim.sync"},
	     [this](entt::registry &registry, double)
	     { ShowBuggyLoads(registry, m_simStepper.Current()); }});
	// Each ship's plumes, from its throttle in the same snapshot, at rest in the frame of the station
	// the ships fly by.
	m_frame.Register(
	    {"shell.engines",
	     TickPhase::Sync,
	     Access::Of<ShipEngines>().Writing<Scene::ParticleEmitterComponent>(),
	     {"sim.sync"},
	     [this](entt::registry &registry, double)
	     {
		     entt::entity station = entt::null;
		     for (const auto [entity, celestial] :
		          registry.view<const Scene::CelestialBodyComponent>().each())
			     if (celestial.body == m_stationBody)
				     station = entity;
		     ShowEngines(registry, m_simStepper.Current(), station);
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
	             Scene::ClipPlayerComponent, Scene::NodePoseComponent, WorldTransformComponent>(),
	     {},
	     [this](entt::registry &registry, double deltaTime)
	     {
		     JETTISON_PROFILE_ZONE("scene.update");
		     m_clipEvents.clear();
		     m_sceneUpdateSystem->Update(registry, static_cast<float>(deltaTime), &m_clipEvents);
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
		     BindSequenceRoles(registry);
		     m_sequences->Update(registry, deltaTime);
		     ShowSequence(registry);
	     }});
	// A jump's streaks ahead of this frame's camera, after it has moved.
	m_frame.Register(
	    {"shell.streaks",
	     TickPhase::Presentation,
	     Access::Of<Scene::CameraPoseComponent>()
	         .Writing<TransformComponent, WorldTransformComponent>(),
	     {"camera.sequence"},
	     [this](entt::registry &registry, double) { PlaceJumpStreaks(registry); }});
	// After the cameras, so the listener is where this frame's camera is, and after the world
	// transforms the sources follow.
	m_frame.Register(
	    {"audio.update",
	     TickPhase::Presentation,
	     Access::Of<
	         WorldTransformComponent, Scene::CameraPoseComponent, Scene::CelestialBodyComponent>()
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
	// What is picked outlined, and the hull flown from its seat marked as the cockpit, on the proxies
	// the sync phase left (MarkScene).
	m_frame.Register(
	    {"shell.marks",
	     TickPhase::Presentation,
	     Access::Of<Scene::SimProxyComponent>()
	         .Writing<Scene::OutlineComponent, Scene::MaskComponent>(),
	     {},
	     [this](entt::registry &registry, double) { MarkScene(registry); }});
	// The game UI, from the snapshots the sync phase took in: the market screen's data, then layout
	// and drawing into the overlay.
	m_frame.Register(
	    {"ui.update",
	     TickPhase::Presentation,
	     {},
	     {},
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
		    m_assets, m_arguments.assets, m_userPaths.userData, m_userPaths.config / kModListFile);
	m_pIoWorker = std::make_unique<Jobs::IoWorker>();
	m_pResources = std::make_unique<Resource::ResourceManager>(m_assets, *m_pIoWorker);
	Animation::RegisterRigLoader(*m_pResources);
	Animation::RegisterClipFileLoader(*m_pResources);
	m_definitions.emplace(m_assets);
	LoadTradeDefinitions(*m_definitions);
	LoadTraderDefinitions(*m_definitions);
	Script::LoadDefinitions(*m_definitions);
	m_pScripts = std::make_unique<Script::ScriptHost>(m_assets);
	m_pScripts->SendCommandsTo(m_sim);
	m_pScripts->LoadAll(*m_definitions);
	m_definitions->Load<Dock::HullDefinition>(
	    Dock::kHullDirectory, Serialise::FormatVersion{Dock::kDefinitionVersion});
	m_definitions->Load<Dock::StationLayout>(
	    Dock::kStationDirectory, Serialise::FormatVersion{Dock::kDefinitionVersion});
	m_pDocks->Define(*m_definitions);
	m_sceneUpdateSystem.emplace(m_pResources.get());
	Scene::RegisterSequenceLoader(*m_pResources);
	m_sequences.emplace(*m_pResources);
	Scene::RegisterJumpLoaders(*m_pResources);
	m_jump.emplace(*m_pResources, [this](Scene::JumpArrival &arrival) { ShowArrival(arrival); });
}


void Application::InitScene()
{
	m_pScene = std::make_unique<Scene::Scene>(m_pResources.get());
	InitGalaxy();
	DefineStarSystems();
	// The traders' tasks read the systems; their trees name the tasks.
	const auto *decision = m_definitions->Find<Ai::DecisionDefinition>(kTradeDecision);
	const std::optional<std::string> unanswered =
	    decision ? UnansweredInput(*decision, m_pScripts.get()) : std::nullopt;
	if (unanswered)
		Core::Log(
		    Core::Severity::Error, kShell,
		    "The decision '{}' asks for '{}', which neither a trader nor a script answers; the galaxy "
		    "has "
		    "no traders",
		    kTradeDecision, *unanswered);
	else if (decision)
	{
		RegisterTraders(
		    m_sim, *m_pBehaviours, *m_pPhysics, *m_pLanes, m_galaxySystems, *decision,
		    m_pScripts.get());
		m_pBehaviours->Define(*m_definitions);
	}
	else
		Core::Log(
		    Core::Severity::Warning, kShell, "No decision '{}' is defined; the galaxy has no traders",
		    kTradeDecision);
	LoadScene();
	OpenStations();
	if (const std::size_t gates = App::OpenGates(m_sim, *m_pGates, m_galaxySystems).size(); gates > 0)
		Core::Log(Core::Severity::Info, kShell, "{} gates open", gates);
	SpawnShip();
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

	Ui::DataModel &market = m_pUi->CreateDataModel("market");
	m_pMarketModel = &market;
	market.Bind("screen", &m_marketScreen);
	market.BindEvent(
	    "buy",
	    [this](std::span<const Ui::EventArgument> arguments)
	    {
		    SendTrade(
		        arguments.empty() ? "" : Ui::AsText(arguments[0]),
		        arguments.size() < 2 ? 1 : Ui::AsInteger(arguments[1]));
	    });
	market.BindEvent(
	    "sell",
	    [this](std::span<const Ui::EventArgument> arguments)
	    {
		    SendTrade(
		        arguments.empty() ? "" : Ui::AsText(arguments[0]),
		        -(arguments.size() < 2 ? 1 : Ui::AsInteger(arguments[1])));
	    });
	market.BindEvent(
	    "lot",
	    [this](std::span<const Ui::EventArgument> arguments)
	    {
		    m_lot = std::max<std::int64_t>(arguments.empty() ? 1 : Ui::AsInteger(arguments[0]), 1);
		    m_marketDirty = true;
	    });
	m_marketDocument = m_pUi->LoadDocument("ui/market.rml");

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
	// The headless run shows both screens from its first frame, so the overlay's path is drawn in
	// every stage the render test walks, and the save stage lists its save on one of them.
	ShowMarket(m_arguments.smokeTest);
	ShowSaves(m_arguments.smokeTest);
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
	// The station the ship is bound to hums, from wherever the system scene put it; a jump's new
	// system scene makes a new one, which hums once it is there.
	if (!m_stationBody.empty())
		for (const auto [entity, celestial] :
		     registry.view<const Scene::CelestialBodyComponent>().each())
			if (celestial.body == m_stationBody && !registry.all_of<Audio::SoundComponent>(entity))
			{
				Audio::SoundComponent hum;
				hum.sound = kStationHum;
				hum.volume = kStationHumVolume;
				hum.falloff = kStationHumFalloff;
				registry.emplace<Audio::SoundComponent>(entity, std::move(hum));
			}
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
	// The player's own ship's events, the ones about it by its "ship" field: another ship's docking or
	// a trader's trade (phase 9) is not the player's to hear from the market screen.
	const entt::meta_type type = event.event.type();
	const entt::meta_data ship = type ? Reflect::FindField(type, "ship") : entt::meta_data{};
	if (!ship)
		return;
	const entt::meta_any value = ship.get(event.event);
	const auto *pShip = value.try_cast<entt::entity>();
	if (pShip == nullptr || *pShip != m_ship)
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


void Application::ShowMarket(bool shown)
{
	m_showMarket = shown;
	if (!m_pUi)
		return;
	if (shown)
	{
		m_marketDirty = true;
		m_pUi->Show(m_marketDocument);
	}
	else
		m_pUi->Hide(m_marketDocument);
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


entt::entity Application::ShownMarket() const
{
	const Sim::Snapshot &snapshot = m_simStepper.Current();
	const auto markets = snapshot.Components<Market>();
	if (m_station != entt::null)
		for (const auto &market : markets)
			if (market.value.stationEntity == m_station)
				return market.entity;
	if (m_systemScene)
	{
		const std::uint64_t shown = Sim::SystemId(m_systemScene->System().name);
		for (const auto &market : markets)
			if (market.value.system == shown)
				return market.entity;
	}
	return entt::null;
}


void Application::SendTrade(std::string_view good, std::int64_t crates)
{
	const entt::entity market = ShownMarket();
	if (m_ship == entt::null || market == entt::null || good.empty() || crates == 0)
		return;
	m_sim.Commands().Push(Trade{kPilot, m_ship, market, std::string{good}, crates});
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

	// The screen is made again only when a step brought a new snapshot or the shell's side of it
	// moved, and the model told only when what it shows differs, so a still market costs nothing.
	const Sim::Snapshot &snapshot = m_simStepper.Current();
	if (m_showMarket && (m_marketDirty || m_marketStep != snapshot.step))
	{
		MarketView view;
		view.market = ShownMarket();
		view.ship = m_ship;
		view.system = m_systemScene ? m_systemScene->System().name : std::string{};
		view.docked = m_docked;
		view.lot = m_lot;
		view.message = m_tradeMessage;
		MarketScreen screen = BuildMarketScreen(snapshot, *m_definitions, m_pUi->Strings(), view);
		if (screen != m_marketScreen)
		{
			m_marketScreen = std::move(screen);
			m_pMarketModel->Changed("screen");
		}
		m_marketStep = snapshot.step;
		m_marketDirty = false;
	}
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
	// As typed into the console, and a line it refuses fails the run: a frame captured from the wrong
	// place is worse than none.
	for (const std::string &line : m_arguments.commands)
	{
		const std::size_t before = m_console.Lines().size();
		m_console.Execute(line);
		const std::vector<DevTools::ConsoleLine> lines = m_console.Lines();
		for (std::size_t index = before; index < lines.size(); ++index)
			if (lines[index].kind == DevTools::ConsoleLine::Kind::Error)
				throw std::runtime_error(std::format("--run '{}': {}", line, lines[index].text));
	}

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


void Application::Tick()
{
	JETTISON_PROFILE_FUNCTION();
	const bool loading = m_pResources->Loading() != 0;
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
	if (shoot)
	{
		m_screenshotTaken = true;
		RequestClose();
	}
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
	std::cerr << std::format(
	    "Benchmark: {} on {} at {}x{}, {} frames after {} to warm up, {} GPU frames measured, depth "
	    "prepass {}, ambient occlusion {}, bloom {}\n"
	    "GPU time per pass (mean):\n{}"
	    "Wall time per frame: {:.3f} ms\n"
	    "Meshes {}: {} outside the view, {} behind a horizon, {} too small to see; {} draws of {} "
	    "triangles\n"
	    "Shadow casters {} / {} / {} / {} in {} / {} / {} / {} draws, {} triangles\n"
	    "Lights {}, {} listed\n"
	    "Transparent: {} draws; {} emitters in {} particle slots\n"
	    "Outlines {}\n",
	    !m_arguments.systemPath.empty() ? m_arguments.systemPath
	    : m_arguments.scenePath.empty() ? std::string{kDefaultScene}
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
	if (m_input.WasPressedThisFrame(m_jumpNext))
		Jump();
	if (m_input.WasPressedThisFrame(m_quickSave))
		SaveTo("quicksave");
	if (m_input.WasPressedThisFrame(m_quickLoad))
		LoadFrom("quicksave");
	if (m_input.WasPressedThisFrame(m_toggleMarket) && !(m_pUi && m_pUi->WantsKeyboard()))
		ShowMarket(!m_showMarket);
	if (m_input.WasPressedThisFrame(m_toggleSaves) && !(m_pUi && m_pUi->WantsKeyboard()))
		ShowSaves(!m_showSaves);
	// A click on the scene, not on a window over it, ImGui's or the game UI's.
	if (m_input.WasPressedThisFrame(m_pickAction) && !uiMouse)
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
		else if (m_showSaves)
			ShowSaves(false);
		else if (m_showMarket)
			ShowMarket(false);
		else if (!m_pScene->Registry().view<Scene::SequencePlayerComponent>().empty())
			StopSequence();
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
	if (!m_arguments.systemPath.empty())
		m_pScene->LoadSystem(m_arguments.systemPath);
	else if (!m_arguments.scenePath.empty())
		m_pScene->Load(m_arguments.scenePath);
	else
	{
		// The assets' own scene, through the mounts, so a packed build and a mod's scene are read
		// as a directory's.
		const auto bytes = m_assets.Read(kDefaultScene);
		if (!bytes)
			throw std::runtime_error(std::format("Scene '{}': the assets have none", kDefaultScene));
		try
		{
			m_pScene->LoadText({reinterpret_cast<const char *>(bytes->data()), bytes->size()});
		}
		catch (const std::exception &error)
		{
			throw std::runtime_error(std::format("Scene '{}': {}", kDefaultScene, error.what()));
		}
	}
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
		if (!m_pCruises->HasStarSystem(Sim::SystemId(m_systemScene->System().name)))
			m_pCruises->DefineStarSystem(m_systemScene->System());
		SetProxyFactory();
		DrawStations();
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
		m_pLanes->SetGalaxy(*m_galaxy);
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
		if (!m_jump->Begin(
		        *m_galaxy, static_cast<std::size_t>(system),
		        lane == exits.end() ? std::string{} : std::string{lane->arrival}))
			return false;
		// Straight to a system no lane leads to is the tools' tour, and leaves the ship behind.
		if (lane != exits.end())
			TakeShipAlong(*lane);
		return true;
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
	if (!m_jump->Begin(*m_galaxy, exit.destination, std::string{exit.arrival}))
		return false;
	TakeShipAlong(exit);
	return true;
}


void Application::TakeShipAlong(const Celestial::Exit &exit)
{
	if (!CanPilot())
		return;
	m_sim.Commands().Push(Ai::EnterLane{m_ship, std::string{exit.jumpPoint}});
}


void Application::RequestTransit(std::string_view jumpPoint)
{
	const Sim::Snapshot &snapshot = m_simStepper.Current();
	const Sim::TransformSample *ship = m_ship == entt::null ? nullptr : snapshot.FindSample(m_ship);
	if (!ship || ship->system == Sim::kNoSystem)
	{
		Core::Log(Core::Severity::Warning, kShell, "No transit: the ship is in no system");
		return;
	}
	const std::string name = jumpPoint.empty() ? ShipFrame() : std::string{jumpPoint};
	for (const auto &[entity, gate] : snapshot.Components<Ai::Gate>())
		if (const Sim::TransformSample *at = snapshot.FindSample(entity);
		    gate.jumpPoint == name && at && at->system == ship->system)
		{
			m_sim.Commands().Push(Ai::RequestTransit{kPilot, m_ship, entity});
			return;
		}
	Core::Log(
	    Core::Severity::Warning, kShell, "No transit: {}",
	    jumpPoint.empty() ? std::string{"the ship is not at a gate; name one"}
	                      : std::format("the ship's system has no gate '{}'", jumpPoint));
}


void Application::FollowShipAlong(const Ai::OnLane &lane)
{
	if (!m_galaxy || !m_shownSystem || m_jump->Active() ||
	    Sim::SystemId(m_galaxy->systems[*m_shownSystem].name) != lane.from)
		return;
	const auto to = std::ranges::find(
	    m_galaxy->systems, lane.to,
	    [](const Celestial::GalaxySystem &entry) { return Sim::SystemId(entry.name); });
	if (to == m_galaxy->systems.end())
		return;
	m_jumpStep = m_sim.Clock().Step();
	m_jump->Begin(*m_galaxy, static_cast<std::size_t>(to - m_galaxy->systems.begin()), lane.arrival);
}


void Application::LogTraders() const
{
	// From the snapshot, as the market screen is: presentation sees the traders as they were shown.
	const Sim::Snapshot &snapshot = m_simStepper.Current();
	const auto stationOf = [&](entt::entity market)
	{
		const Market *found = market == entt::null ? nullptr : snapshot.Get<Market>(market);
		return found ? found->station : std::string{"nowhere"};
	};
	const std::span<const Sim::ComponentSample<Trader>> traders = snapshot.Components<Trader>();
	if (traders.empty())
		Core::Log(Core::Severity::Info, kShell, "No traders");
	for (const auto &[entity, trader] : traders)
	{
		const auto *hold = snapshot.Get<Hold>(entity);
		const auto *purse = PurseOf(snapshot, entity);
		const Sim::TransformSample *sample = snapshot.FindSample(entity);
		std::string where{"on a lane"};
		if (m_galaxy && sample && sample->system != Sim::kNoSystem)
			for (const Celestial::GalaxySystem &system : m_galaxy->systems)
				if (Sim::SystemId(system.name) == sample->system)
					where = "in " + system.name;
		const std::string run =
		    trader.good.empty()
		        ? std::string{"choosing a run"}
		        : std::format(
		              "{} {} {}, bound for {} with {} jumps to go", trader.good,
		              trader.buyAt == entt::null ? "held, to sell at"
		                                         : "from " + stationOf(trader.buyAt) + " to",
		              stationOf(trader.sellAt), stationOf(trader.bound), trader.jumps.size());
		Core::Log(
		    Core::Severity::Info, kShell,
		    "Trader {:#x}, {}: {}; {} crates aboard, {} credits; {} runs made {}",
		    entt::to_integral(entity), where, run, hold ? hold->Used() : 0, purse ? purse->credits : 0,
		    trader.runs, trader.earned);
	}
}


void Application::LogRoute(std::string_view systemName) const
{
	if (!m_galaxy || m_galaxySystems.size() != m_galaxy->systems.size())
	{
		Core::Log(Core::Severity::Warning, kShell, "No route: there is no galaxy");
		return;
	}
	const int to = m_galaxy->Find(systemName);
	const auto from = std::ranges::find(
	    m_galaxy->systems, m_shipSystem,
	    [](const Celestial::GalaxySystem &entry) { return Sim::SystemId(entry.name); });
	if (to < 0 || m_ship == entt::null || from == m_galaxy->systems.end())
	{
		Core::Log(
		    Core::Severity::Warning, kShell, "No route: {}",
		    to < 0 ? std::format("the galaxy has no system '{}'", systemName)
		           : std::string{"the ship is in no system of the galaxy"});
		return;
	}
	// From the station when the ship is bound to one in its system, and to the destination's first.
	const auto index = static_cast<std::size_t>(from - m_galaxy->systems.begin());
	const Celestial::StarSystem &destination = m_galaxySystems[static_cast<std::size_t>(to)];
	const auto station =
	    std::ranges::find(destination.bodies, Celestial::BodyKind::Station, &Celestial::Body::kind);
	const Ai::Place start{index, m_stationSystem == m_shipSystem ? m_stationBody : std::string{}};
	const Ai::Place end{
	    static_cast<std::size_t>(to),
	    station == destination.bodies.end() ? std::string{} : station->name};
	constexpr double kClearance = 1.0e5;
	const float speed = Physics::Thrusters{}.maxSpeed;
	const std::optional<Ai::Route> route = Ai::PlotRoute(
	    *m_galaxy, start, end,
	    Ai::CrossingByCourse(m_galaxySystems, m_sim.Clock().Now(), speed, kClearance));
	if (!route)
	{
		Core::Log(Core::Severity::Info, kShell, "No lanes lead to {}", systemName);
		return;
	}
	std::string jumps;
	for (const Celestial::Exit &jump : route->jumps)
		jumps += std::format(
		    "{}through {} to {} ({:.0f} s)", jumps.empty() ? "" : ", ", jump.jumpPoint,
		    m_galaxy->systems[jump.destination].name, jump.transitSeconds);
	Core::Log(
	    Core::Severity::Info, kShell, "Route to {}{}: {}; {:.0f} s at {:.0f} m/s", systemName,
	    end.body.empty() ? "" : " (" + end.body + ")", jumps.empty() ? "no jumps" : jumps,
	    route->seconds, speed);
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
	DrawStations();
	// A jump along a lane took the ship with it (TakeShipAlong), and it comes out of the gate the
	// cameras frame; one straight to a system leaves it behind.
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
		for (const auto [entity, celestial] :
		     registry.view<const Scene::CelestialBodyComponent>().each())
			if (celestial.body == name)
				body = entity;
		if (body == entt::null)
			throw std::invalid_argument{std::format("there is no body '{}' in the scene", name)};
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


void Application::BindSequenceRoles(entt::registry &registry)
{
	// What the shell binds, of what a sequence may name, each frame, since a proxy is made by the
	// sync and a pad's clamps are shown closed from a snapshot: a sequence that names another role is
	// refused, and one whose role the shell does not show yet waits for it.
	entt::entity ship = entt::null;
	entt::entity pad = entt::null;
	entt::entity station = entt::null;
	for (const auto [entity, proxy] : registry.view<const Scene::SimProxyComponent>().each())
		if (m_ship != entt::null && proxy.simEntity == m_ship)
			ship = entity;
	for (const StationDressing &dressing : m_dressings)
	{
		if (dressing.body == m_stationBody)
			station = dressing.station;
		for (const PadClamps &clamps : dressing.clamps)
			if (m_ship != entt::null && clamps.ship == m_ship)
				pad = clamps.player;
	}
	for (const auto [camera, player] : registry.view<Scene::SequencePlayerComponent>().each())
	{
		player.roles.insert_or_assign("ship", ship);
		player.roles.insert_or_assign("pad", pad);
		player.roles.insert_or_assign("station", station);
	}
}


void Application::ShowSequence(entt::registry &registry)
{
	const auto players = registry.view<const Scene::SequencePlayerComponent>();
	const Scene::SequencePlayerComponent *player =
	    players.empty() ? nullptr : &players.get<const Scene::SequencePlayerComponent>(players.front());
	m_pRenderer->SetLetterbox(player != nullptr ? player->letterbox : 0.0f);
	// A jump's fade is set in the sync phase, before this, and is the one shown while it is up.
	if (player != nullptr && player->fade > 0.0f && !m_jump->Active())
		m_pRenderer->SetFade(player->fadeColor, player->fade);
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
	// The screens' text the shell writes is made again in it, as the trade's answer is when it next
	// comes.
	m_marketDirty = true;
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
		save.info.mods = m_mods;
		save.AddSection(
		    std::string{kShellSection},
		    WriteShellState({m_ship, m_station, m_shipSystem, m_stationBody}));
		save.AddSection(std::string{Script::kStoreSection}, Script::WriteStore(m_pScripts->Store()));
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
		if (save.info.galaxy != m_galaxyPath)
			throw std::runtime_error(
			    std::format(
			        "'{}' is of the galaxy '{}', and this run's is '{}'", name, save.info.galaxy,
			        m_galaxyPath));
		// Everything read before the world is replaced, so a save that does not fit changes nothing.
		const Sim::SaveSection *section = save.FindSection(kShellSection);
		const ShellState shell = section ? ReadShellState(section->data) : ShellState{};
		// A save from before scripts kept keys holds none.
		const Sim::SaveSection *keys = save.FindSection(Script::kStoreSection);
		Script::ScriptStore store = keys ? Script::ReadStore(keys->data) : Script::ScriptStore{};
		m_sim.Load(save.world);
		// A save from before purses were their owners' holds them on the ships.
		if (const std::size_t moved = MovePursesToOwners(m_sim.Registry()); moved > 0)
			Core::Log(
			    Core::Severity::Info, kShell, "'{}' kept {} purses on ships, now their owners'", name,
			    moved);
		// And one from before there were gates has none.
		if (const std::size_t gates = App::OpenGates(m_sim, *m_pGates, m_galaxySystems).size();
		    gates > 0)
			Core::Log(Core::Severity::Info, kShell, "'{}' had no gates; {} are open", name, gates);
		m_pScripts->SetStore(std::move(store));
		// Other mods may make another game of the save, which is the player's to judge: it loads.
		for (const std::string &difference : Sim::ModDifferences(save.info.mods, m_mods))
			Core::Log(
			    Core::Severity::Warning, kShell, "'{}' was saved with other mods: {}", name,
			    difference);

		StopPiloting();
		const entt::registry &world = m_sim.Registry();
		const auto valid = [&world](entt::entity entity)
		{ return world.valid(entity) ? entity : entt::null; };
		m_ship = valid(shell.ship);
		m_station = valid(shell.station);
		// The steered ships but its own, which the docks steer as they fly it in and out.
		m_traffic.clear();
		for (const entt::entity entity : world.view<const Ai::Steered>())
			if (entity != m_ship)
				m_traffic.push_back(entity);
		std::ranges::sort(m_traffic, {}, [](entt::entity entity) { return entt::to_integral(entity); });
		// The systems from the world, which a ship saved on a lane or before its station was bound
		// holds as the shell's section does not.
		const auto systemOf = [&world](entt::entity entity)
		{
			const auto *inSystem =
			    entity == entt::null ? nullptr : world.try_get<Sim::InSystem>(entity);
			return inSystem ? inSystem->system : Sim::kNoSystem;
		};
		m_shipSystem = m_ship == entt::null ? shell.shipSystem : systemOf(m_ship);
		m_stationSystem = systemOf(m_station);
		m_stationBody = shell.stationBody;
		m_resumePiloting = false;
		m_docked = Dock::IsDocked(world, m_ship, m_station);
		FindHullProxies();
		m_sentControls.reset();
		m_pickPending = false;
		m_picked.reset();
		m_tradeMessage.clear();
		m_marketDirty = true;
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
	{
		if (!StartPiloting(cockpit ? Scene::PilotView::Cockpit : Scene::PilotView::Chase))
			return false;
		// Traffic about the station, whose proxies the frames draw from here on and whose steering
		// the save and load carries; and the ship let go of its pad and lifted clear by the docks.
		if (!cockpit)
		{
			SpawnTraffic(4);
			if (m_docked)
				ReleasePad();
		}
		return true;
	};
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
	actions.trade = [this]() -> std::optional<bool>
	{
		const entt::entity market = ShownMarket();
		const auto *shown = m_simStepper.Current().Get<Market>(market);
		if (!shown || shown->goods.empty() || m_ship == entt::null)
			return std::nullopt;
		ShowMarket(true);
		// Laid out with the screen made from this snapshot, so what it shows is what the snapshot has.
		UpdateGameUi();
		const std::string station = m_pUi->ElementText(m_marketDocument, "station");
		const std::string stock = m_pUi->ElementText(m_marketDocument, "stock-0");
		if (station != shown->station || stock != std::to_string(shown->goods.front().stock))
		{
			Core::Log(
			    Core::Severity::Error, kShell,
			    "The market screen shows '{}' with {} crates, and the snapshot has '{}' with {}",
			    station, stock, shown->station, shown->goods.front().stock);
			return false;
		}
		m_smokeTradeAnswers = m_tradeAnswers;
		m_smokeUiSounds = UiSounds();
		return m_pUi->Click(m_marketDocument, "buy-0");
	};
	actions.trading = [this] { return m_tradeAnswers == m_smokeTradeAnswers; };
	actions.traded = [this]
	{
		const std::string shown = m_pUi->ElementText(m_marketDocument, "message");
		if (shown != m_tradeMessage)
			Core::Log(
			    Core::Severity::Error, kShell,
			    "The market screen says '{}', and the trade's answer was '{}'", shown, m_tradeMessage);
		// The answer is heard as well as shown: filled or refused, it plays on the UI bus.
		if (UiSounds() == m_smokeUiSounds)
			Core::Log(
			    Core::Severity::Error, kShell, "The trade's answer '{}' played no sound",
			    m_tradeMessage);
		return !m_tradeMessage.empty() && shown == m_tradeMessage && UiSounds() > m_smokeUiSounds;
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
		// The window's callbacks go before the window does.
		m_glfwInput.Detach();
		// The UI holds no device object: the renderer's copies of its textures go with the renderer.
		if (m_pAudio)
			m_soundSources.Clear(*m_pAudio);
		m_eventSounds.reset();
		m_pAudio.reset();
		m_pMarketModel = nullptr;
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
	// The system scene holds the scene, and goes first. The scene's rigs are resource handles,
	// released as it goes; then the manager, which holds no
	// GPU object once the renderer has released its handles, goes before the worker it posts to.
	m_systemScene.reset();
	m_pScene.reset();
	m_sceneUpdateSystem.reset();
	m_sequences.reset();
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
		const std::uint64_t id = Sim::SystemId(system.name);
		m_galaxySystems.push_back(system);
		if (!m_pLanes->HasStarSystem(id))
			m_pLanes->DefineStarSystem(system);
		if (!m_pCruises->HasStarSystem(id))
			m_pCruises->DefineStarSystem(system);
		if (!m_pGates->HasStarSystem(id))
			m_pGates->DefineStarSystem(system);
		if (!m_pPhysics->HasStarSystem(id))
			m_pPhysics->DefineStarSystem(std::move(system));
	}
}


bool Application::BindStation()
{
	if (!m_systemScene)
		return false;
	const Celestial::StarSystem &system = m_systemScene->System();
	const auto station =
	    std::ranges::find(system.bodies, Celestial::BodyKind::Station, &Celestial::Body::kind);
	if (station == system.bodies.end())
		return false;
	const std::uint64_t systemId = Sim::SystemId(system.name);
	if (m_station != entt::null && m_stationSystem == systemId && m_stationBody == station->name)
		return true;
	entt::registry &world = m_sim.Registry();
	const auto bind = [&](entt::entity entity)
	{
		m_station = entity;
		m_stationSystem = systemId;
		m_stationBody = station->name;
		Core::Log(Core::Severity::Info, kShell, "The ship is bound to {}", m_stationBody);
		return true;
	};

	// Built by the docks to its layout (OpenStations).
	if (const entt::entity built = FindStation(world, systemId, station->name); built != entt::null)
		return bind(built);
	// A station of no layout, made on an earlier visit or in the world a save loaded: a body on the
	// station's rails that is neither a pad nor a ship clamped to one.
	for (const auto [entity, rails, inSystem, body] :
	     world
	         .view<const Physics::OnRails, const Sim::InSystem, const Physics::Body>(
	             entt::exclude<Dock::Hull>)
	         .each())
		if (inSystem.system == systemId && rails.body == station->name)
			return bind(entity);

	entt::registry &scene = m_pScene->Registry();
	entt::entity shown = entt::null;
	for (const auto [entity, celestial] : scene.view<const Scene::CelestialBodyComponent>().each())
		if (celestial.body == station->name)
			shown = entity;
	if (shown == entt::null)
		return false;

	// The station's hull, as the scene draws it.
	const std::string shape = "stations/" + Scene::BodyEntityId(station->name);
	const std::optional<CollisionMesh> mesh = MakeCollisionMesh(scene, shown);
	if (!mesh)
	{
		Core::Log(
		    Core::Severity::Warning, kShell, "No station: '{}' is drawn with no shape to meet",
		    station->name);
		return false;
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
		    Core::Severity::Info, kShell, "Station '{}' collides as {} triangles, with no pads",
		    station->name, mesh->shape.indices.size() / 3);
	}

	// Where the station is now: placed by its rails from the next step, and by the shell for the
	// snapshots the stepper is resynced to.
	const Physics::OnRails rails{station->name};
	m_station = world.create();
	world.emplace<Sim::Transform>(
	    m_station, m_pPhysics->RailsPlace(systemId, rails, m_sim.Clock().Now()));
	world.emplace<Sim::InSystem>(m_station, systemId);
	world.emplace<Physics::OnRails>(m_station, rails);
	world.emplace<Physics::LocalFrame>(m_station, station->name);
	world.emplace<Physics::Body>(m_station, Physics::Body{Physics::BodyKind::Kinematic, shape});
	m_simStepper.Resync();
	return bind(m_station);
}


void Application::OpenStations()
{
	// Once a run, as the ship is: a load replaces them with the save's.
	entt::registry &world = m_sim.Registry();
	if (!world.view<Dock::Station>().empty() || !world.view<Market>().empty())
	{
		FindHullProxies();
		return;
	}
	const std::vector<entt::entity> stations = App::OpenStations(m_sim, *m_pDocks, m_galaxySystems);
	std::size_t pads = 0;
	for (const entt::entity station : stations)
		pads += world.get<Dock::Station>(station).pads.size();
	Core::Log(
	    Core::Severity::Info, kShell, "{} stations open, with {} pads and {} ships parked",
	    stations.size(), pads, world.view<const Dock::Hull>().size());

	const std::vector<entt::entity> markets = OpenMarkets(m_sim, *m_definitions);
	for (const entt::entity entity : markets)
	{
		Market &market = world.get<Market>(entity);
		market.stationEntity = FindStation(world, market.system, market.station);
	}
	Core::Log(Core::Severity::Info, kShell, "{} markets open", markets.size());
	if (m_pBehaviours->HasTree(kTraderTree))
		Core::Log(
		    Core::Severity::Info, kShell, "{} traders open", OpenTraders(m_sim, *m_pDocks).size());
	FindHullProxies();
	m_simStepper.Resync();
}


void Application::FindHullProxies()
{
	m_hullProxies.clear();
	for (const auto [entity, hull] : m_sim.Registry().view<const Dock::Hull>().each())
		if (const Dock::HullDefinition *definition = m_pDocks->FindHull(hull.definition);
		    definition && entity != m_ship && std::ranges::find(m_traffic, entity) == m_traffic.end())
			m_hullProxies.emplace(entity, hull.definition);
}


void Application::SpawnShip()
{
	if (m_ship != entt::null || !BindStation())
		return;
	entt::registry &world = m_sim.Registry();
	const Sim::Transform at = world.get<Sim::Transform>(m_station);

	// In the station's frame, flown by its pilot.
	m_ship = world.create();
	world.emplace<Sim::Transform>(m_ship, at);
	world.emplace<Sim::InSystem>(m_ship, m_stationSystem);
	world.emplace<Physics::LocalFrame>(m_ship, m_stationBody);
	MakeShip(world, *m_pDocks, m_ship, kShipHull, kShipMass, true);
	world.replace<Physics::FlightControls>(m_ship, Physics::FlightControls{{}, {}, true, m_station});
	world.emplace<Sim::ControlledBy>(m_ship, kPilot);
	world.emplace<Hold>(m_ship, Hold{kShipHold, {}});
	// The player's credits are their own, not the ship's, and outlast it.
	if (FindOwner(world, kPilot) == entt::null)
		MakeOwner(world, kPilot, kStartingCredits);
	m_shipSystem = m_stationSystem;

	// Docked on the lowest numbered free pad it fits; at a station of none, off its -Z end, nose
	// towards it.
	const auto *station = world.try_get<Dock::Station>(m_station);
	const Dock::HullDefinition *hull = m_pDocks->FindHull(kShipHull);
	const auto pad = station && hull ? std::ranges::find_if(
	                                       station->pads,
	                                       [&](entt::entity entity)
	                                       {
		                                       const Dock::Pad &found = world.get<Dock::Pad>(entity);
		                                       return found.state == Dock::PadState::Free &&
		                                              !found.display && found.padClass >= hull->pad;
	                                       })
	                                 : std::vector<entt::entity>::const_iterator{};
	if (station && hull && pad != station->pads.end())
	{
		m_pDocks->Park(m_sim, m_ship, *pad);
		m_docked = true;
		Core::Log(
		    Core::Severity::Info, kShell,
		    "The ship is docked on pad {} of {}: undock lifts it clear, and P flies it",
		    world.get<Dock::Pad>(*pad).number, m_stationBody);
	}
	else
	{
		const double standOff =
		    m_pPhysics->ShapeRadius(world.get<Physics::Body>(m_station).shape) + kShipStandOff;
		world.replace<Sim::Transform>(
		    m_ship, Sim::Transform{
		                at.position + at.orientation * glm::dvec3{0.0, 0.0, -standOff},
		                at.orientation * glm::angleAxis(glm::pi<double>(), glm::dvec3{0.0, 1.0, 0.0})});
		Core::Log(
		    Core::Severity::Info, kShell, "A ship holds station off {}: P flies it", m_stationBody);
	}
	m_simStepper.Resync();
}


void Application::RequestPad(bool fly)
{
	if (m_ship == entt::null || m_station == entt::null)
	{
		Core::Log(Core::Severity::Warning, kShell, "No ship, or no station to ask for a pad");
		return;
	}
	m_sim.Commands().Push(Dock::RequestPad{kPilot, m_ship, m_station, fly});
}


void Application::ReleasePad()
{
	if (m_ship != entt::null)
		m_sim.Commands().Push(Dock::ReleasePad{kPilot, m_ship});
}


void Application::DrawStations()
{
	m_dressings.clear();
	m_dressedStations.clear();
	if (!m_systemScene)
		return;
	entt::registry &scene = m_pScene->Registry();
	for (const Celestial::Body &body : m_systemScene->System().bodies)
	{
		const Dock::StationLayout *layout =
		    body.kind == Celestial::BodyKind::Station ? m_pDocks->FindLayout(body.layout) : nullptr;
		if (!layout)
			continue;
		for (const auto [entity, celestial] : scene.view<const Scene::CelestialBodyComponent>().each())
			if (celestial.body == body.name)
			{
				m_dressings.push_back(DressStation(scene, entity, *layout, *m_pDocks));
				m_dressedStations.push_back(entt::null);
			}
	}
}


void Application::ShowPadStates()
{
	if (m_dressings.empty() || !m_systemScene)
		return;
	const Sim::Snapshot &snapshot = m_simStepper.Current();
	const std::uint64_t system = Sim::SystemId(m_systemScene->System().name);
	for (std::size_t index = 0; index < m_dressings.size(); ++index)
	{
		entt::entity &station = m_dressedStations[index];
		if (App::ShowPadStates(
		        m_pScene->Registry(), m_dressings[index], snapshot, station, *m_pDocks) != 0)
			continue;
		// Not yet found, or a world loaded over the one it was found in: this frame's snapshot is the
		// world's latest step, so the station found now is the one its pads name.
		station = FindStation(m_sim.Registry(), system, m_dressings[index].body);
		if (station != entt::null)
			App::ShowPadStates(m_pScene->Registry(), m_dressings[index], snapshot, station, *m_pDocks);
	}
}


void Application::SpawnTraffic(int count)
{
	entt::registry &world = m_sim.Registry();
	std::erase_if(m_traffic, [&world](entt::entity entity) { return !world.valid(entity); });
	if (count == 0)
	{
		for (const entt::entity entity : m_traffic)
			world.destroy(entity);
		Core::Log(Core::Severity::Info, kShell, "{} shuttles taken out of the world", m_traffic.size());
		m_traffic.clear();
		m_simStepper.Resync();
		return;
	}
	if (!BindStation())
	{
		Core::Log(Core::Severity::Warning, kShell, "No station in the system shown to send traffic to");
		return;
	}
	const std::size_t room = kMostTraffic - m_traffic.size();
	const std::size_t sent = std::min(static_cast<std::size_t>(count), room);
	if (sent == 0)
	{
		Core::Log(Core::Severity::Warning, kShell, "Already {} shuttles, the most", kMostTraffic);
		return;
	}
	// The ring is clear of the hull by more than a shuttle's clearance and separation together, and
	// its places, in the station's axes, a sixteenth of it apart: 100 m or so, twice what two
	// shuttles keep between them.
	const Sim::Transform at = world.get<Sim::Transform>(m_station);
	const double ring = m_pPhysics->ShapeRadius(world.get<Physics::Body>(m_station).shape) + 150.0;
	std::vector<bool> taken(kMostTraffic, false);
	for (const entt::entity entity : m_traffic)
		if (const auto *destination = world.try_get<Ai::Destination>(entity))
			for (std::size_t place = 0; place < kMostTraffic; ++place)
				if (glm::distance(destination->offset, TrafficPlace(place, ring)) < 1.0)
					taken[place] = true;
	// Drawn from a generator of the shell's, so sending traffic does not move the world's.
	Math::Random random{m_sim.Clock().Step(), m_traffic.size()};
	std::size_t place = 0;
	for (std::size_t index = 0; index < sent; ++index)
	{
		while (taken[place])
			++place;
		taken[place] = true;
		glm::dvec3 direction{0.0};
		while (glm::length(direction) < 0.1 || glm::length(direction) > 1.0)
			direction = {
			    random.NextDouble() * 2.0 - 1.0, random.NextDouble() * 2.0 - 1.0,
			    random.NextDouble() * 2.0 - 1.0};
		const double distance = 1000.0 + random.NextDouble() * 1000.0;

		const entt::entity shuttle = world.create();
		world.emplace<Sim::Transform>(
		    shuttle,
		    Sim::Transform{at.position + glm::normalize(direction) * distance, at.orientation});
		world.emplace<Sim::InSystem>(shuttle, m_stationSystem);
		world.emplace<Physics::LocalFrame>(shuttle, m_stationBody);
		MakeShip(world, *m_pDocks, shuttle, kShipHull, kShipMass, true);
		world.emplace<Ai::Steered>(shuttle);
		Ai::Destination destination;
		destination.entity = m_station;
		destination.offset = TrafficPlace(place, ring);
		destination.speed = kTrafficSpeed;
		world.emplace<Ai::Destination>(shuttle, destination);
		m_traffic.push_back(shuttle);
	}
	std::ranges::sort(m_traffic, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	m_simStepper.Resync();
	Core::Log(
	    Core::Severity::Info, kShell, "{} shuttles sent to hold station by {}, {} in all", sent,
	    m_stationBody, m_traffic.size());
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
	// Against the station while the ship is in its frame; against its frame elsewhere, on the way
	// to the next system or off another body after a cruise.
	controls.reference =
	    m_stationSystem == m_shipSystem && ShipFrame() == m_stationBody ? m_station : entt::null;
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
	const std::string frame = ShipFrame();
	for (const auto [entity, celestial] : registry.view<const Scene::CelestialBodyComponent>().each())
		if (!frame.empty() && celestial.body == frame)
			pilot->frame = entity;
}


void Application::ShowJumpStreaks()
{
	entt::registry &registry = m_pScene->Registry();
	// The scene a jump arrives in is another, and the streaks the old one's.
	if (!registry.valid(m_jumpStreaks))
		m_jumpStreaks = entt::null;
	const Scene::JumpStage stage = m_jump->Stage();
	const bool streaking = stage == Scene::JumpStage::FadingOut || stage == Scene::JumpStage::FadingIn;
	if (!m_jump->Active())
	{
		if (m_jumpStreaks != entt::null)
			registry.destroy(m_jumpStreaks);
		m_jumpStreaks = entt::null;
		return;
	}
	if (m_jumpStreaks == entt::null)
	{
		if (!streaking)
			return;
		m_jumpStreaks = registry.create();
		registry.emplace<Scene::BuiltComponent>(m_jumpStreaks);
		registry.emplace<Scene::TransformComponent>(m_jumpStreaks);
		registry.emplace<Scene::LayerComponent>(m_jumpStreaks, Scene::Layer::Effects);
		// At rest in their own frame, which keeps ahead of the camera, so they stream past it
		// whatever it does.
		Scene::ParticleEmitterComponent streaks;
		streaks.rate = 700.0f;
		streaks.lifetime = 0.32f;
		streaks.speed = kStreaksSpeed;
		streaks.speedJitter = 0.2f;
		streaks.radius = kStreaksRadius;
		streaks.size = {0.12f, 0.12f};
		streaks.stretch = 0.05f;
		// A streak spreads its light along its 16 m, which a 12 cm particle's middle line holds
		// three-quarters of a percent of: bright at its middle, it is a line of about 4.
		streaks.color = {0.7f, 0.85f, 1.0f, 500.0f};
		streaks.colorEnd = streaks.color;
		streaks.capacity = 512;
		streaks.seed = 21;
		registry.emplace<Scene::ParticleEmitterComponent>(m_jumpStreaks, streaks);
	}
	registry.get<Scene::ParticleEmitterComponent>(m_jumpStreaks).active = streaking;
}


void Application::PlaceJumpStreaks(entt::registry &registry)
{
	if (m_jumpStreaks == entt::null || !registry.valid(m_jumpStreaks))
		return;
	// Ahead of this frame's camera, turned to face it, so its -Z, which it sends its particles along,
	// is back towards the camera. Its world transform is written here too, after the rigs: the camera
	// moves tens of kilometres a second with the system's bodies, and one placed by the last frame's
	// pose would be hundreds of metres behind it.
	const Render::CameraView camera = Render::ActiveCameraView(registry);
	const glm::dquat facing =
	    camera.orientation * glm::angleAxis(glm::pi<double>(), glm::dvec3{0.0, 1.0, 0.0});
	const glm::dvec3 at = camera.position + camera.orientation * glm::dvec3{0.0, 0.0, -kStreaksAhead};
	glm::vec3 rotation{0.0f};
	glm::extractEulerAngleXYZ(
	    glm::mat4{glm::mat4_cast(glm::quat{facing})}, rotation.x, rotation.y, rotation.z);
	auto &transform = registry.get<Scene::TransformComponent>(m_jumpStreaks);
	transform.translation = at;
	transform.rotation = rotation;
	glm::dmat4 world = glm::mat4_cast(facing);
	world[3] = glm::dvec4{at, 1.0};
	registry.emplace_or_replace<Scene::WorldTransformComponent>(
	    m_jumpStreaks, Scene::WorldTransformComponent{world});
}


void Application::SetProxyFactory()
{
	m_systemScene->SetProxyFactory(
	    [this](entt::registry &registry, entt::entity simEntity)
	    {
		    const bool traffic = std::ranges::binary_search(
		        m_traffic, entt::to_integral(simEntity), {},
		        [](entt::entity entity) { return entt::to_integral(entity); });
		    // The ship and its traffic are shuttles; a mod without the hull still sees them, as boxes.
		    if (simEntity == m_ship || traffic)
		    {
			    const Dock::HullDefinition *shuttle = m_pDocks->FindHull(kShipHull);
			    return MakeHullProxy(registry, shuttle ? *shuttle : Dock::HullDefinition{}, simEntity);
		    }
		    if (m_simStepper.Current().Get<Dock::Buggy>(simEntity))
			    return MakeBuggyProxy(registry, simEntity);
		    const auto parked = m_hullProxies.find(simEntity);
		    const Dock::HullDefinition *hull =
		        parked == m_hullProxies.end() ? nullptr : m_pDocks->FindHull(parked->second);
		    return hull ? MakeHullProxy(registry, *hull, simEntity) : entt::entity{entt::null};
	    });
}


void Application::DrawFlight()
{
	// A cinematic has the frame to itself.
	if (!m_pScene->Registry().view<Scene::SequencePlayerComponent>().empty())
		return;
	// Speeds from the snapshots, which are what presentation may see: the ship's motion relative to
	// the station's over the last step.
	const Sim::Snapshot &previous = m_simStepper.Previous();
	const Sim::Snapshot &current = m_simStepper.Current();
	const auto *shipSample = current.FindSample(m_ship);
	const auto *stationSample = current.FindSample(m_station);
	// A station a system away, as it is until the ship's arrival binds it to the next, is no measure.
	if (!shipSample || !stationSample || shipSample->system != stationSample->system)
		return;
	const Sim::Transform *ship = &shipSample->transform;
	// Off another body after a cruise, the ship is measured against that body, where its rails have
	// it at each snapshot's time, since the station may be a planet's breadth away.
	std::string against = m_stationBody;
	glm::dvec3 reference = stationSample->transform.position;
	std::optional<glm::dvec3> referenceBefore;
	if (const auto *stationBefore = previous.Find(m_station))
		referenceBefore = stationBefore->position;
	const std::string frame = ShipFrame();
	if (!frame.empty() && frame != m_stationBody && m_systemScene &&
	    Sim::SystemId(m_systemScene->System().name) == shipSample->system)
		if (const int body = m_systemScene->System().Find(frame); body >= 0)
		{
			const Celestial::StarSystem &system = m_systemScene->System();
			against = frame;
			reference =
			    Celestial::EvaluateBody(system, static_cast<std::size_t>(body), current.time).metres;
			referenceBefore =
			    Celestial::EvaluateBody(system, static_cast<std::size_t>(body), previous.time).metres;
		}
	double speed = 0.0;
	if (const auto *shipBefore = previous.Find(m_ship);
	    shipBefore && referenceBefore && current.time > previous.time)
		speed = glm::length((ship->position - shipBefore->position) - (reference - *referenceBefore)) /
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
		if (const auto *cruising = current.Get<Ai::Cruising>(m_ship))
			ImGui::Text(
			    "Cruising to %s  %.0f km/s  %.0f s", cruising->target.c_str(), speed / 1000.0,
			    static_cast<double>(
			        cruising->arrivalStep - std::min(cruising->arrivalStep, current.step)) *
			        m_sim.FixedStep());
		ImGui::Text(
		    "%s  %.1f m/s  %.0f m", against.c_str(), speed, glm::length(ship->position - reference));
		if (m_docked)
			ImGui::Text("Docked at %s", m_stationBody.c_str());
		if (const auto *transit = current.Get<Ai::Transit>(m_ship))
			if (const auto *gate = current.Get<Ai::Gate>(transit->gate))
			{
				if (transit->cleared)
					ImGui::Text(
					    "Cleared through %s  %.0f s", gate->jumpPoint.c_str(),
					    static_cast<double>(gate->lapses - std::min(gate->lapses, current.step)) *
					        m_sim.FixedStep());
				else
					ImGui::Text(
					    "Waiting for %s, %d in the queue", gate->jumpPoint.c_str(),
					    static_cast<int>(
					        std::ranges::find(gate->queue, m_ship) - gate->queue.begin() + 1));
			}
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
		m_pScripts->Dispatch(event);
		PlayEventSound(event);
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
		else if (const auto *entered = event.event.try_cast<Ai::LaneEntered>();
		         entered && entered->entity == m_ship)
		{
			m_shipSystem = Sim::kNoSystem;
			m_resumePiloting = m_piloting;
			if (m_piloting)
				StopPiloting();
			Core::Log(
			    Core::Severity::Info, kShell,
			    "The ship entered the lane through {}, out at {} in {:.0f} s", entered->lane.departure,
			    entered->lane.arrival, entered->transitSeconds);
			// Sent by a gate rather than the tools' jump, which has the scene jumping already.
			FollowShipAlong(entered->lane);
		}
		else if (const auto *cleared = event.event.try_cast<Ai::TransitCleared>();
		         cleared && cleared->ship == m_ship)
			Core::Log(
			    Core::Severity::Info, kShell,
			    "{} clears the ship through for {} credits: fly through its ring within {:.0f} s",
			    Describe(cleared->gate), cleared->toll, Ai::Gates::kClearanceSeconds);
		else if (const auto *waiting = event.event.try_cast<Ai::TransitQueued>();
		         waiting && waiting->ship == m_ship)
			Core::Log(
			    Core::Severity::Info, kShell, "The ship waits for {}, {} in the queue",
			    Describe(waiting->gate), waiting->place);
		else if (const auto *noTransit = event.event.try_cast<Ai::TransitRefused>();
		         noTransit && noTransit->ship == m_ship)
			Core::Log(
			    Core::Severity::Warning, kShell, "{} will not clear the ship: {}",
			    Describe(noTransit->gate), Word(noTransit->reason));
		else if (const auto *unused = event.event.try_cast<Ai::TransitLapsed>();
		         unused && unused->ship == m_ship)
			Core::Log(
			    Core::Severity::Warning, kShell, "The ship's clearance through {} has lapsed",
			    Describe(unused->gate));
		else if (const auto *sent = event.event.try_cast<Ai::TransitSent>();
		         sent && sent->ship == m_ship)
			Core::Log(
			    Core::Severity::Info, kShell, "{} sends the ship through, charging {} credits",
			    Describe(sent->gate), sent->toll);
		else if (const auto *passed = event.event.try_cast<Ai::GatePassed>();
		         passed && passed->ship == m_ship)
			Core::Log(
			    Core::Severity::Warning, kShell, "The ship passed through {} unsent: {}",
			    Describe(passed->gate), Word(passed->reason));
		else if (const auto *arrived = event.event.try_cast<Ai::LaneLeft>();
		         arrived && arrived->entity == m_ship)
		{
			m_shipSystem = arrived->system;
			Core::Log(Core::Severity::Info, kShell, "The ship came out at {}", arrived->arrival);
		}
		else if (const auto *cruise = event.event.try_cast<Ai::CruiseStarted>();
		         cruise && cruise->ship == m_ship)
			Core::Log(
			    Core::Severity::Info, kShell, "The ship cruises to {}, {:.0f} km in {:.1f} s",
			    cruise->body, cruise->metres / 1000.0, cruise->seconds);
		else if (const auto *cruised = event.event.try_cast<Ai::CruiseEnded>();
		         cruised && cruised->ship == m_ship)
			Core::Log(Core::Severity::Info, kShell, "The ship has arrived off {}", cruised->body);
		else if (const auto *noCruise = event.event.try_cast<Ai::CruiseRefused>();
		         noCruise && noCruise->ship == m_ship)
			Core::Log(
			    Core::Severity::Warning, kShell, "The ship cannot cruise to {}: {}", noCruise->body,
			    Word(noCruise->reason));
		else if (const auto *noLane = event.event.try_cast<Ai::LaneRefused>();
		         noLane && noLane->entity == m_ship)
			Core::Log(
			    Core::Severity::Warning, kShell, "The ship cannot enter the lane through {}: {}",
			    noLane->jumpPoint, Word(noLane->reason));
		// The console's traffic holds station where it arrives; a trader arriving is on its way on.
		else if (const auto *holding = event.event.try_cast<Ai::Arrived>();
		         holding && std::ranges::binary_search(
		                        m_traffic, entt::to_integral(holding->ship), {},
		                        [](entt::entity entity) { return entt::to_integral(entity); }))
			Core::Log(
			    Core::Severity::Info, kShell, "{} holds station by {}", Describe(holding->ship),
			    m_stationBody);
		else if (const auto *landed = event.event.try_cast<Dock::Landed>();
		         landed && landed->ship == m_ship)
		{
			m_docked = true;
			Core::Log(
			    Core::Severity::Info, kShell, "The ship is clamped to pad {} of {}", landed->number,
			    m_stationBody);
			PlaySequence(kDockSequence, 0.0);
		}
		else if (const auto *left = event.event.try_cast<Dock::Left>(); left && left->ship == m_ship)
		{
			m_docked = false;
			Core::Log(
			    Core::Severity::Info, kShell, "The clamps of pad {} of {} let the ship go",
			    left->number, m_stationBody);
		}
		else if (const auto *given = event.event.try_cast<Dock::PadGiven>();
		         given && given->ship == m_ship)
			Core::Log(
			    Core::Severity::Info, kShell, "{} gives the ship pad {}", m_stationBody, given->number);
		else if (const auto *queued = event.event.try_cast<Dock::PadQueued>();
		         queued && queued->ship == m_ship)
			Core::Log(
			    Core::Severity::Info, kShell, "The ship waits for a pad at {}, {} in the queue",
			    m_stationBody, queued->place);
		else if (const auto *noPad = event.event.try_cast<Dock::PadRefused>();
		         noPad && noPad->ship == m_ship)
			Core::Log(Core::Severity::Warning, kShell, "No pad: {}", Word(noPad->reason));
		else if (const auto *lapsed = event.event.try_cast<Dock::PadLapsed>();
		         lapsed && lapsed->ship == m_ship)
			Core::Log(
			    Core::Severity::Warning, kShell, "Pad {} was not landed on in time, and is given up",
			    lapsed->number);
		else if (const auto *carried = event.event.try_cast<Dock::CargoCarried>();
		         carried && carried->ship == m_ship)
			Core::Log(
			    Core::Severity::Info, kShell, "The ship's cargo has crossed the deck of pad {} of {}",
			    carried->number, m_stationBody);
		else if (const auto *released = event.event.try_cast<Dock::PadReleased>();
		         released && released->ship == m_ship)
		{
			// Sent along a lane from its pad, the clamps let it go without a Left.
			m_docked = false;
			Core::Log(Core::Severity::Info, kShell, "The ship gives up its pad at {}", m_stationBody);
		}
		else if (const auto *traded = event.event.try_cast<Traded>(); traded && traded->ship == m_ship)
		{
			const std::int64_t crates = traded->crates < 0 ? -traded->crates : traded->crates;
			++m_tradeAnswers;
			m_tradeMessage = m_pUi->Strings().Plural(
			    traded->crates > 0 ? "market.bought" : "market.sold", crates,
			    GoodName(*m_definitions, m_pUi->Strings(), traded->good),
			    m_pUi->Strings().Number(traded->price));
			Core::Log(
			    Core::Severity::Info, kShell, "{}; {} credits left", m_tradeMessage,
			    FormatCredits(traded->credits));
		}
		else if (const auto *refused = event.event.try_cast<TradeRefused>();
		         refused && refused->ship == m_ship)
		{
			++m_tradeAnswers;
			m_tradeMessage = m_pUi->Strings().Format("market.refused", Word(refused->reason));
			Core::Log(Core::Severity::Info, kShell, "{}", m_tradeMessage);
		}
		if (m_showMarket)
			m_marketDirty = true;
	}
	// Out of a lane into the system shown, whichever came first, the ship is bound to the station
	// there, and flown again if it was flown in.
	if (CanPilot() && m_stationSystem != m_shipSystem)
		BindStation();
	if (m_resumePiloting && CanPilot())
	{
		m_resumePiloting = false;
		StartPiloting(m_pilotView);
	}
}


void Application::MarkScene(entt::registry &registry)
{
	// The marks the shell placed last frame come off, and this frame's go on: the proxies of the
	// simulation's entities come and go with its snapshots, so a mark is placed on what is there now.
	// Marks a scene file authored are its own, and stay.
	for (const auto [entity, owned] : m_shellMarks)
		if (registry.valid(entity))
		{
			if (owned.outline)
				registry.remove<Scene::OutlineComponent>(entity);
			if (owned.mask)
				registry.remove<Scene::MaskComponent>(entity);
		}
	m_shellMarks.clear();
	const auto proxyOf = [&](entt::entity simEntity)
	{
		for (const auto [entity, sim] : registry.view<const Scene::SimProxyComponent>().each())
			if (sim.simEntity == simEntity)
				return entity;
		return entt::entity{entt::null};
	};
	// What was picked is selected, a line about it; while flying, it is the ship's target, a glow.
	if (m_picked)
		if (const entt::entity proxy = proxyOf(m_picked->entity);
		    proxy != entt::null && !registry.all_of<Scene::OutlineComponent>(proxy))
		{
			Scene::OutlineComponent outline;
			if (m_piloting)
			{
				outline.style = Scene::OutlineStyle::Targeted;
				outline.color = {1.0f, 0.2f, 0.1f, 2.0f};
				outline.width = 14.0f;
			}
			registry.emplace<Scene::OutlineComponent>(proxy, outline);
			m_shellMarks.push_back({proxy, {true, false}});
		}
	// From the seat, the ship's own hull is the cockpit, which an outline is not drawn over.
	if (m_piloting && m_pilotView == Scene::PilotView::Cockpit)
		if (const entt::entity proxy = proxyOf(m_ship);
		    proxy != entt::null && !registry.all_of<Scene::MaskComponent>(proxy))
		{
			registry.emplace<Scene::MaskComponent>(proxy, Scene::MaskComponent{.cockpit = true});
			const auto found = std::ranges::find_if(
			    m_shellMarks, [&](const auto &mark) { return mark.first == proxy; });
			if (found != m_shellMarks.end())
				found->second.mask = true;
			else
				m_shellMarks.push_back({proxy, {false, true}});
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


std::string Application::Word(const Sim::Reason &reason) const
{
	return WordReason(reason, m_simStepper.Current(), *m_definitions, m_pUi->Strings());
}


std::string Application::Describe(entt::entity simEntity) const
{
	if (simEntity == m_ship)
		return "Ship";
	if (simEntity == m_station)
		return m_stationBody;
	if (const auto found = std::ranges::find(m_traffic, simEntity); found != m_traffic.end())
		return std::format("Shuttle {}", found - m_traffic.begin() + 1);
	if (const auto *gate = m_simStepper.Current().Get<Ai::Gate>(simEntity))
		return gate->jumpPoint;
	return std::format("Entity {:#x}", entt::to_integral(simEntity));
}


std::string Application::ShipFrame() const
{
	const auto *frame =
	    m_ship == entt::null ? nullptr : m_simStepper.Current().Get<Physics::LocalFrame>(m_ship);
	return frame ? frame->body : std::string{};
}


glm::dvec3 Application::PilotFrameOrigin(double simSeconds) const
{
	const std::string frame = ShipFrame();
	if (frame.empty() || !CanPilot())
		return glm::dvec3{0.0};
	const Celestial::StarSystem &system = m_systemScene->System();
	const int index = system.Find(frame);
	return index < 0
	           ? glm::dvec3{0.0}
	           : Celestial::EvaluateBody(system, static_cast<std::size_t>(index), simSeconds).metres;
}
} // namespace Jettison::App
