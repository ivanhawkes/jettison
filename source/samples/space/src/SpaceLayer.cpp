#include "SpaceLayer.h"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/app/AppArguments.h>
#include <jettison/app/SmokeSequence.h>
#include <jettison/audio/SoundSources.h>
#include <jettison/core/Log.h>
#include <jettison/devtools/Console.h>
#include <jettison/dock/Docks.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/math/Random.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/render/Camera.h>
#include <jettison/render/DebugDraw.h>
#include <jettison/render/SceneView.h>
#include <jettison/sample/SpaceGame.h>
#include <jettison/scene/Scene.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Gates.h>
#include <jettison/space/Lanes.h>
#include <jettison/space/Routes.h>
#include <jettison/space/StarSystemFrame.h>
#include <jettison/spatial/SystemIndices.h>
#include <jettison/vfs/FileSystem.h>

#include "Stations.h"

namespace Jettison::Sample
{
namespace
{
constexpr Core::Category kSpace{"space"};

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

// At a station of no pads the ship holds 150 m clear of the station's hull with its own, nose
// towards it.
constexpr double kShipStandOff = 150.0;

// The most traffic the console sends to a station, and how fast it flies: a shuttle's full 150 m/s
// crosses the ring's 2 km in a quarter of a minute, too fast to watch a ship give way.
constexpr std::size_t kMostTraffic = 16;
constexpr float kTrafficSpeed = 80.0f;

// How long the camera eases into and out of the ship's rigs, and between them.
constexpr double kPilotTransition = 1.5;
constexpr double kViewTransition = 0.5;
// The cinematic the ship landing on a pad plays (cinematics/dock.sequence.yaml).
constexpr std::string_view kDockSequence = "dock";

// The hum of the station the ship is bound to, heard at its full volume within a few hundred metres
// of the station's centre, as a station is that size, and fading to a murmur two kilometres out.
constexpr std::string_view kStationHum = "audio/effects/station-hum.wav";
constexpr Audio::Falloff kStationHumFalloff{300.0f, 20000.0f, 1.0f};
constexpr float kStationHumVolume = 0.6f;

constexpr Physics::FlightControls kSmokeFlight{{0.0f, 0.0f, -0.2f}, {0.0f, 0.05f, 0.1f}};


// A place on the traffic's ring about a station, in its axes: about its Z, the axis its dock is
// at the end of, so the ship holding off the dock is clear of every one.
glm::dvec3 TrafficPlace(std::size_t place, double ring)
{
	const double angle = glm::two_pi<double>() * static_cast<double>(place) / kMostTraffic;
	return {ring * std::cos(angle), ring * std::sin(angle), 0.0};
}


// The console's words after the command, as one name.
std::string Joined(std::span<const std::string> arguments)
{
	std::string name;
	for (const std::string &word : arguments)
		name += (name.empty() ? "" : " ") + word;
	return name;
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


// The time presentation shows: between the last two snapshots' times.
double ShownTime(const App::Shell &shell)
{
	return Celestial::PresentationTime(shell.Previous().time, shell.Snapshot().time, shell.Alpha());
}


// The scene's entity of the proxy of a simulation entity, or null.
entt::entity ProxyOf(const entt::registry &registry, entt::entity simEntity)
{
	for (const auto [entity, sim] : registry.view<const Scene::SimProxyComponent>().each())
		if (sim.simEntity == simEntity)
			return entity;
	return entt::null;
}
} // namespace


SpaceLayer::SpaceLayer(SpaceGame &game, const App::ShellWorld &world, App::Shell &shell)
    : m_game{game}, m_shell{shell}, m_sim{world.world}, m_physics{world.physics}
{
	m_pLanes = &Space::LaneTravel::Attach(m_sim, m_physics);
	m_pGates = &Space::Gates::Attach(m_sim, m_physics, *m_pLanes);
	m_pCruises = &Space::CruiseTravel::Attach(m_sim, m_physics);
	Spatial::SystemIndices &indices = Spatial::SystemIndices::Attach(m_sim);
	Ai::Steering::Attach(m_sim, m_physics, indices);
	m_pDocks = &Dock::Docks::Attach(m_sim, m_physics);
	m_pBehaviours = &Ai::Behaviours::Attach(m_sim);

	Input::InputSystem &input = shell.Input();
	m_jumpNext = input.DefineAction("jump_next", Input::ActionType::Button);
	m_pilot = input.DefineAction("pilot", Input::ActionType::Button);
	m_shipNext = input.DefineAction("ship_next", Input::ActionType::Button);
	m_shipPrevious = input.DefineAction("ship_previous", Input::ActionType::Button);
	m_helm = input.DefineAction("helm", Input::ActionType::Button);
	using Input::Keys::Character;
	input.Bind("shell", m_jumpNext, {Character('j')});
	input.Bind("shell", m_pilot, {Character('p')});
	input.Bind("shell", m_pilot, {{Input::Device::GamepadButton, 7}});
	input.Bind("shell", m_shipNext, {Character(']')});
	input.Bind("shell", m_shipPrevious, {Character('[')});
	input.Bind("shell", m_helm, {Character('h')});
	m_pilotInput.emplace(input);
}


SpaceLayer::~SpaceLayer() = default;


SpaceWorld SpaceLayer::World() const
{
	return {m_sim, m_physics, *m_pLanes, *m_pGates, *m_pDocks, *m_pBehaviours};
}


void SpaceLayer::LoadDefinitions(Sim::DefinitionRegistry &definitions)
{
	definitions.Load<Dock::PadClassDefinition>(
	    Dock::kPadClassDirectory, Serialise::FormatVersion{Dock::kDefinitionVersion});
	definitions.Load<Dock::HullDefinition>(
	    Dock::kHullDirectory, Serialise::FormatVersion{Dock::kDefinitionVersion});
	definitions.Load<Dock::StationLayout>(
	    Dock::kStationDirectory, Serialise::FormatVersion{Dock::kDefinitionVersion});
}


void SpaceLayer::Define(const Sim::DefinitionRegistry &definitions)
{
	m_pDocks->Define(definitions);
}


void SpaceLayer::OpenGalaxy(const Sim::DefinitionRegistry &definitions)
{
	InitGalaxy();
	DefineStarSystems();
	m_game.OpenGalaxy(World(), m_galaxySystems, definitions, m_shell.Scripts());
	SpaceScene::RegisterJumpLoaders(m_shell.Resources());
	m_jump.emplace(
	    m_shell.Resources(), [this](SpaceScene::JumpArrival &arrival) { ShowArrival(arrival); });
}


void SpaceLayer::AddCommands(DevTools::Console &console)
{
	console.RegisterCommand(
	    "jump",
	    "Jumps to the star system named, or out through the next lane, taking the ship along a "
	    "lane as a tool, past the gate and its toll: jump [system]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { Jump(Joined(arguments)); });
	console.RegisterCommand(
	    "cruise", "Sends the ship on a cruise to the body of its system named: cruise <body>",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    if (!CanPilot())
			    Core::Log(Core::Severity::Warning, kSpace, "No cruise: no ship in the system shown");
		    else
			    m_sim.Commands().Push(Space::Cruise{m_shell.Player(), m_ship, Joined(arguments)});
	    });
	console.RegisterCommand(
	    "transit",
	    "Asks a gate of the ship's system to clear it through, the one whose frame it is in unless "
	    "another is named; flown through the ring, the gate sends it and charges the lane's toll: "
	    "transit [gate]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { RequestTransit(Joined(arguments)); });
	console.RegisterCommand(
	    "route", "Says the quickest way from the ship to the star system named: route <system>",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { LogRoute(Joined(arguments)); });
	console.RegisterCommand(
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
				        Core::Severity::Warning, kSpace, "traffic takes a count, not '{}'",
				        arguments[0]);
				    return;
			    }
		    }
		    SpawnTraffic(count);
	    });
	console.RegisterCommand(
	    "dock",
	    "Asks the ship's station for a pad and flies the ship onto it, or leaves the landing to the "
	    "pilot: dock [manual]",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    { RequestPad(arguments.empty() || arguments[0] != "manual"); });
	console.RegisterCommand(
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
			        Core::Severity::Warning, kSpace, "pilot takes chase, cockpit or off, not '{}'",
			        view);
	    });
	console.RegisterCommand(
	    "undock", "Lifts the ship 40 m off its pad and gives the pad up, or gives up one not landed on",
	    [this](DevTools::Console &, std::span<const std::string>) { ReleasePad(); });
	console.RegisterCommand(
	    "fleet", "Lists the ships of the fleet: where each is, who flies it and what it holds",
	    [this](DevTools::Console &console, std::span<const std::string>) { ListFleet(console); });
	console.RegisterCommand(
	    "attach",
	    "Follows a ship of the fleet, by its number in the fleet's list, as ] and [ do: attach <n>",
	    [this](DevTools::Console &, std::span<const std::string> arguments)
	    {
		    std::size_t number = 0;
		    const auto [end, error] =
		        arguments.size() == 1
		            ? std::from_chars(
		                  arguments[0].data(), arguments[0].data() + arguments[0].size(), number)
		            : std::from_chars_result{nullptr, std::errc::invalid_argument};
		    const std::vector<entt::entity> fleet = FleetOf(m_shell.Snapshot(), m_shell.Player());
		    if (error != std::errc{} || end != arguments[0].data() + arguments[0].size() ||
		        number == 0 || number > fleet.size())
			    throw std::invalid_argument{
			        std::format("takes a ship's number in the fleet, from 1 to {}", fleet.size())};
		    Follow(fleet[number - 1]);
	    });
	console.RegisterCommand(
	    "helm",
	    "Takes the attached ship's helm, its behaviour standing down, or leaves it to trade, as H does",
	    [this](DevTools::Console &, std::span<const std::string>) { ToggleHelm(); });
}


void SpaceLayer::RegisterFrame(Ecs::Scheduler &frame)
{
	using Ecs::Access;
	using Ecs::TickPhase;
	using Scene::TransformComponent;

	// The pilot's controls, as the commands the next step applies, ahead of the shell's pick.
	Ecs::SystemDescription flight{
	    "space.flight", TickPhase::Commands, {}, {}, [this](entt::registry &, double) { FlyShip(); }};
	flight.before = {"shell.pick"};
	frame.Register(std::move(flight));

	// A jump's stages, first in the sync phase: on the frame its effect covers the screen it replaces
	// the scene, which the syncs after it then place and fill as they do any other.
	Ecs::SystemDescription jump{
	    "space.jump",
	    TickPhase::Sync,
	    {},
	    {},
	    [this](entt::registry &, double deltaTime)
	    {
		    m_jump->Advance(deltaTime);
		    const float fade = m_jump->Fade();
		    m_shell.SetFade(glm::mix(kJumpFlash, kJumpColor, glm::smoothstep(0.4f, 1.0f, fade)), fade);
		    ShowJumpStreaks();
		    const bool active = m_jump->Active();
		    if (m_jumpWasActive && !active && m_jump->Error().empty())
			    Core::Log(
			        Core::Severity::Info, kSpace, "The simulation ran {} steps through the jump",
			        m_sim.Clock().Step() - m_jumpStep);
		    m_jumpWasActive = active;
	    }};
	jump.before = {"sim.sync"};
	frame.Register(std::move(jump));

	// The syncs after the shell's, from the same snapshot the traffic layer drew, and before the
	// steps' events are taken.
	const auto sync =
	    [&frame](std::string name, Access access, std::function<void(entt::registry &, double)> run)
	{
		Ecs::SystemDescription system{
		    std::move(name), TickPhase::Sync, std::move(access), {"sim.sync"}, std::move(run)};
		system.before = {"sim.events"};
		frame.Register(std::move(system));
	};
	// Bodies on rails, at the moment presentation shows: between the last two steps, in time.
	sync(
	    "celestial.sync",
	    Access::Of<Scene::CelestialBodyComponent>().Writing<Scene::PlacementComponent>(),
	    [this](entt::registry &, double)
	    {
		    if (m_systemScene)
			    m_systemScene->Place(ShownTime(m_shell));
	    });
	// The stations' lamps.
	sync(
	    "space.pads", Access::Of<>().Writing<Scene::MeshComponent>(),
	    [this](entt::registry &, double) { ShowPadStates(); });
	// What each buggy carries.
	sync(
	    "space.buggies", Access::Of<BuggyLoad>().Writing<TransformComponent>(),
	    [this](entt::registry &registry, double) { ShowBuggyLoads(registry, m_shell.Snapshot()); });
	// Each ship's plumes, jets and glowing nozzles, from what its thrusters fired, at rest in the frame
	// of the station the ships fly by.
	sync(
	    "space.thrusters",
	    Access::Of<>().Writing<ShipThrusters, Scene::ParticleEmitterComponent, Scene::MeshComponent>(),
	    [this](entt::registry &registry, double deltaTime)
	    {
		    entt::entity station = entt::null;
		    for (const auto [entity, celestial] :
		         registry.view<const Scene::CelestialBodyComponent>().each())
			    if (celestial.body == m_stationBody)
				    station = entity;
		    ShowThrust(registry, m_shell.Snapshot(), station, deltaTime);
	    });
	frame.Register(
	    {"space.events",
	     TickPhase::Sync,
	     {},
	     {"sim.events"},
	     [this](entt::registry &, double) { AfterEvents(); }});

	Ecs::SystemDescription pilot{
	    "space.pilot",
	    TickPhase::Presentation,
	    Access::Of<Scene::SimProxyComponent, Scene::CameraComponent>()
	        .Writing<Scene::PilotCameraComponent>(),
	    {"scene.update"},
	    [this](entt::registry &registry, double) { SyncPilotCamera(registry); }};
	pilot.before = {"camera.rig"};
	frame.Register(std::move(pilot));
	Ecs::SystemDescription roles{
	    "space.roles",
	    TickPhase::Presentation,
	    Access::Of<Scene::SimProxyComponent>().Writing<Scene::SequencePlayerComponent>(),
	    {"camera.rig"},
	    [this](entt::registry &registry, double) { BindSequenceRoles(registry); }};
	roles.before = {"camera.sequence"};
	frame.Register(std::move(roles));
	// A jump's fade over a sequence's, which the shell shows after the sync phase set the jump's; and
	// its streaks ahead of this frame's camera, after it has moved.
	Ecs::SystemDescription streaks{
	    "space.streaks",
	    TickPhase::Presentation,
	    Access::Of<Scene::CameraPoseComponent>()
	        .Writing<TransformComponent, Scene::WorldTransformComponent>(),
	    {"camera.sequence"},
	    [this](entt::registry &registry, double)
	    {
		    if (m_jump->Active())
		    {
			    const float fade = m_jump->Fade();
			    m_shell.SetFade(
			        glm::mix(kJumpFlash, kJumpColor, glm::smoothstep(0.4f, 1.0f, fade)), fade);
		    }
		    PlaceJumpStreaks(registry);
	    }};
	streaks.before = {"audio.update"};
	frame.Register(std::move(streaks));
	Ecs::SystemDescription hum{
	    "space.hum",
	    TickPhase::Presentation,
	    Access::Of<Scene::CelestialBodyComponent>().Writing<Audio::SoundComponent>(),
	    {"camera.sequence"},
	    [this](entt::registry &registry, double) { HumStation(registry); }};
	hum.before = {"audio.update"};
	frame.Register(std::move(hum));
	frame.Register(
	    {"space.orbits",
	     TickPhase::Presentation,
	     Access::Of<Scene::CelestialBodyComponent>(),
	     {"scene.debug"},
	     [this](entt::registry &registry, double)
	     {
		     if (m_shell.DebugShown() && m_systemScene)
			     DrawOrbits(
			         m_shell.DebugLines(), registry, m_systemScene->System(), ShownTime(m_shell));
	     }});
	frame.Register(
	    {"space.mask",
	     TickPhase::Presentation,
	     Access::Of<Scene::SimProxyComponent>().Writing<Scene::MaskComponent>(),
	     {"shell.marks"},
	     [this](entt::registry &registry, double) { MaskCockpit(registry); }});
}


void SpaceLayer::InitGalaxy()
{
	constexpr std::string_view kDefaultGalaxy = "systems/galaxy.yaml";
	const std::optional<std::string> &named = m_shell.Arguments().galaxyPath;
	const std::string path{named.value_or(std::string{kDefaultGalaxy})};
	const auto bytes = m_shell.Assets().Read(path);
	if (!bytes)
	{
		if (named)
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


void SpaceLayer::DefineStarSystems()
{
	if (!m_galaxy)
		return;
	for (const Celestial::GalaxySystem &entry : m_galaxy->systems)
	{
		const auto bytes = m_shell.Assets().Read(entry.system);
		if (!bytes)
			throw std::runtime_error("Failed to read star system '" + entry.system + "'");
		Celestial::StarSystem system = Celestial::ReadStarSystem(
		    std::string_view{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
		const std::uint64_t id = Sim::FrameId(system.name);
		m_galaxySystems.push_back(system);
		if (!m_pLanes->HasStarSystem(id))
			m_pLanes->DefineStarSystem(system);
		if (!m_pCruises->HasStarSystem(id))
			m_pCruises->DefineStarSystem(system);
		if (!m_pGates->HasStarSystem(id))
			m_pGates->DefineStarSystem(system);
		if (!m_physics.HasFrame(id))
			Space::DefineStarSystem(m_physics, std::move(system));
	}
}


void SpaceLayer::SceneLoaded()
{
	if (const std::string &path = m_shell.Scene().StarSystem(); !path.empty())
	{
		const auto bytes = m_shell.Assets().Read(path);
		if (!bytes)
			throw std::runtime_error("Failed to read the scene's star system '" + path + "'");
		Celestial::StarSystem system = Celestial::ReadStarSystem(
		    std::string_view{reinterpret_cast<const char *>(bytes->data()), bytes->size()});
		// A scene of a system the galaxy does not list is the system physics knows of it.
		if (!m_physics.HasFrame(Sim::FrameId(system.name)))
			Space::DefineStarSystem(m_physics, system);
		if (!m_pCruises->HasStarSystem(Sim::FrameId(system.name)))
			m_pCruises->DefineStarSystem(system);
		BuildSystemScene(std::move(system));
	}
	FindShownSystem();
	MarkTargets();
	if (m_opened)
		return;
	m_opened = true;
	OpenStations();
	if (const std::size_t gates = OpenGates(m_sim, *m_pGates, m_galaxySystems).size(); gates > 0)
		Core::Log(Core::Severity::Info, kSpace, "{} gates open", gates);
	SpawnShip();
}


void SpaceLayer::SceneUnloading()
{
	// What the old system scene built goes with the scene's old contents, and the mask's proxy with
	// them: its number may be another entity's in the next.
	m_systemScene.reset();
	m_dressings.clear();
	m_dressedStations.clear();
	m_masked = entt::null;
}


void SpaceLayer::BuildSystemScene(Celestial::StarSystem system)
{
	m_systemScene.emplace(m_shell.Scene(), std::move(system));
	SetProxyFactory();
	DrawStations();
}


void SpaceLayer::FindShownSystem()
{
	m_shownSystem.reset();
	m_arrivedAt.clear();
	if (!m_galaxy || !m_systemScene)
		return;
	const int index = m_galaxy->FindBySystemFile(m_shell.Scene().StarSystem());
	if (index < 0)
		return;
	m_shownSystem = static_cast<std::size_t>(index);
	Celestial::CheckJumpPoints(*m_galaxy, *m_shownSystem, m_systemScene->System());
}


void SpaceLayer::MarkTargets()
{
	entt::registry &registry = m_shell.Scene().Registry();
	for (const auto [entity, celestial] : registry.view<const Scene::CelestialBodyComponent>().each())
	{
		App::OrbitTarget target{celestial.body};
		if (m_systemScene)
			if (const int index = m_systemScene->System().Find(celestial.body); index >= 0)
				target.radius = m_systemScene->System().bodies[static_cast<std::size_t>(index)].radius;
		registry.emplace_or_replace<App::OrbitTarget>(entity, std::move(target));
	}
}


void SpaceLayer::WriteSave(Sim::SaveGame &save) const
{
	if (!m_galaxyPath.empty())
		save.info.details.push_back({std::string{kGalaxyDetail}, m_galaxyPath});
	if (m_systemScene)
		save.info.details.push_back({std::string{kSystemDetail}, m_systemScene->System().name});
	save.AddSection(
	    std::string{kShellSection}, WriteShellState({m_ship, m_station, m_shipSystem, m_stationBody}));
}


void SpaceLayer::ReadSave(const Sim::SaveGame &save)
{
	if (const std::string_view galaxy = save.info.Detail(kGalaxyDetail); galaxy != m_galaxyPath)
		throw std::runtime_error(
		    std::format(
		        "'{}' is of the galaxy '{}', and this run's is '{}'", save.info.name, galaxy,
		        m_galaxyPath));
	const Sim::SaveSection *section = save.FindSection(kShellSection);
	m_loading = section ? ReadShellState(section->data) : ShellState{};
}


void SpaceLayer::Loaded()
{
	const ShellState shell = m_loading.value_or(ShellState{});
	m_loading.reset();
	const Sim::Controller player = m_shell.Player();
	// What an older save of the game lacks.
	m_game.UpgradeSave(m_sim.Registry(), shell.ship, player);
	// And one from before there were gates has none.
	if (const std::size_t gates = OpenGates(m_sim, *m_pGates, m_galaxySystems).size(); gates > 0)
		Core::Log(Core::Severity::Info, kSpace, "The save had no gates; {} are open", gates);

	StopPiloting();
	const entt::registry &world = m_sim.Registry();
	const auto valid = [&world](entt::entity entity)
	{ return world.valid(entity) ? entity : entt::null; };
	m_ship = valid(shell.ship);
	m_station = valid(shell.station);
	// The steered ships no behaviour flies, which are neither the fleet's nor the traders'.
	m_traffic.clear();
	for (const entt::entity entity : world.view<const Ai::Steered>(entt::exclude<Ai::Behaviour>))
		if (entity != m_ship)
			m_traffic.push_back(entity);
	std::ranges::sort(m_traffic, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	// The systems from the world, which a ship saved on a lane or before its station was bound
	// holds as the shell section does not.
	const auto systemOf = [&world](entt::entity entity)
	{
		const auto *inSystem = entity == entt::null ? nullptr : world.try_get<Sim::InFrame>(entity);
		return inSystem ? inSystem->frame : Sim::kNoFrame;
	};
	m_shipSystem = m_ship == entt::null ? shell.shipSystem : systemOf(m_ship);
	m_stationSystem = systemOf(m_station);
	m_stationBody = shell.stationBody;
	m_resumePiloting = false;
	m_docked = Dock::IsDocked(world, m_ship, m_station);
	m_sentControls.reset();
	if (m_systemScene)
		m_systemScene->ResetTraffic();
	if (m_galaxy)
		for (const std::uint64_t system : UnknownSystems(world, *m_galaxy))
			Core::Log(
			    Core::Severity::Warning, kSpace,
			    "The save has entities in a system {:#018x} the galaxy does not have", system);
}


void SpaceLayer::PollInput(const Input::InputSystem &input)
{
	if (input.WasPressedThisFrame(m_jumpNext))
		Jump();
	if (input.WasPressedThisFrame(m_shipNext))
		CycleAttached(1);
	if (input.WasPressedThisFrame(m_shipPrevious))
		CycleAttached(-1);
	if (input.WasPressedThisFrame(m_helm))
		ToggleHelm();
	if (input.WasPressedThisFrame(m_pilot))
	{
		if (m_piloting)
			StopPiloting();
		else
			StartPiloting(m_pilotView);
	}
	if (m_piloting && m_pilotInput->AssistToggled(input))
		m_assist = !m_assist;
	if (m_piloting && m_pilotInput->ViewToggled(input))
		m_pilotView = m_pilotView == Scene::PilotView::Chase ? Scene::PilotView::Cockpit
		                                                     : Scene::PilotView::Chase;
}


bool SpaceLayer::Back()
{
	// A sequence playing is the shell's to stop first.
	if (!m_piloting || !m_shell.Scene().Registry().view<Scene::SequencePlayerComponent>().empty())
		return false;
	StopPiloting();
	return true;
}


void SpaceLayer::DrawPanels(bool devTools)
{
	if (devTools)
	{
		// Under the shell's Scene panel, as the same window.
		if (ImGui::Begin("Scene"))
		{
			ImGui::Separator();
			ImGui::Text("%s", m_systemScene ? m_systemScene->System().name.c_str() : "No star system");
			if (m_galaxy)
				DrawGalaxy();
		}
		ImGui::End();
	}
	if (m_piloting)
		DrawFlight();
}


void SpaceLayer::DrawGalaxy()
{
	constexpr std::array<const char *, 5> kStages{
	    "", "fading out", "reading the files", "streaming in", "fading in"};
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


bool SpaceLayer::Jump(std::string_view systemName)
{
	const auto refuse = [](std::string_view why)
	{
		Core::Log(Core::Severity::Warning, kSpace, "No jump: {}", why);
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


void SpaceLayer::TakeShipAlong(const Celestial::Exit &exit)
{
	if (!CanPilot())
		return;
	m_sim.Commands().Push(Space::EnterLane{m_shell.Player(), m_ship, std::string{exit.jumpPoint}});
}


void SpaceLayer::RequestTransit(std::string_view jumpPoint)
{
	const Sim::Snapshot &snapshot = m_shell.Snapshot();
	const Sim::TransformSample *ship = m_ship == entt::null ? nullptr : snapshot.FindSample(m_ship);
	if (!ship || ship->frame == Sim::kNoFrame)
	{
		Core::Log(Core::Severity::Warning, kSpace, "No transit: the ship is in no system");
		return;
	}
	const std::string name = jumpPoint.empty() ? ShipFrame() : std::string{jumpPoint};
	for (const auto &[entity, gate] : snapshot.Components<Space::Gate>())
		if (const Sim::TransformSample *at = snapshot.FindSample(entity);
		    gate.jumpPoint == name && at && at->frame == ship->frame)
		{
			m_sim.Commands().Push(Space::RequestTransit{m_shell.Player(), m_ship, entity});
			return;
		}
	Core::Log(
	    Core::Severity::Warning, kSpace, "No transit: {}",
	    jumpPoint.empty() ? std::string{"the ship is not at a gate; name one"}
	                      : std::format("the ship's system has no gate '{}'", jumpPoint));
}


void SpaceLayer::FollowShipAlong(const Space::OnLane &lane)
{
	if (!m_galaxy || !m_shownSystem || m_jump->Active() ||
	    Sim::FrameId(m_galaxy->systems[*m_shownSystem].name) != lane.from)
		return;
	const auto to = std::ranges::find(
	    m_galaxy->systems, lane.to,
	    [](const Celestial::GalaxySystem &entry) { return Sim::FrameId(entry.name); });
	if (to == m_galaxy->systems.end())
		return;
	m_jumpStep = m_sim.Clock().Step();
	m_jump->Begin(*m_galaxy, static_cast<std::size_t>(to - m_galaxy->systems.begin()), lane.arrival);
}


void SpaceLayer::LogRoute(std::string_view systemName) const
{
	if (!m_galaxy || m_galaxySystems.size() != m_galaxy->systems.size())
	{
		Core::Log(Core::Severity::Warning, kSpace, "No route: there is no galaxy");
		return;
	}
	const int to = m_galaxy->Find(systemName);
	const auto from = std::ranges::find(
	    m_galaxy->systems, m_shipSystem,
	    [](const Celestial::GalaxySystem &entry) { return Sim::FrameId(entry.name); });
	if (to < 0 || m_ship == entt::null || from == m_galaxy->systems.end())
	{
		Core::Log(
		    Core::Severity::Warning, kSpace, "No route: {}",
		    to < 0 ? std::format("the galaxy has no system '{}'", systemName)
		           : std::string{"the ship is in no system of the galaxy"});
		return;
	}
	// From the station when the ship is bound to one in its system, and to the destination's first.
	const auto index = static_cast<std::size_t>(from - m_galaxy->systems.begin());
	const Celestial::StarSystem &destination = m_galaxySystems[static_cast<std::size_t>(to)];
	const auto station =
	    std::ranges::find(destination.bodies, Celestial::BodyKind::Station, &Celestial::Body::kind);
	const Space::Place start{index, m_stationSystem == m_shipSystem ? m_stationBody : std::string{}};
	const Space::Place end{
	    static_cast<std::size_t>(to),
	    station == destination.bodies.end() ? std::string{} : station->name};
	constexpr double kClearance = 1.0e5;
	const float speed = Physics::Thrusters{}.maxSpeed;
	const std::optional<Space::Route> route = Space::PlotRoute(
	    *m_galaxy, start, end,
	    Space::CrossingByCourse(m_galaxySystems, m_sim.Clock().Now(), speed, kClearance));
	if (!route)
	{
		Core::Log(Core::Severity::Info, kSpace, "No lanes lead to {}", systemName);
		return;
	}
	std::string jumps;
	for (const Celestial::Exit &jump : route->jumps)
		jumps += std::format(
		    "{}through {} to {} ({:.0f} s)", jumps.empty() ? "" : ", ", jump.jumpPoint,
		    m_galaxy->systems[jump.destination].name, jump.transitSeconds);
	Core::Log(
	    Core::Severity::Info, kSpace, "Route to {}{}: {}; {:.0f} s at {:.0f} m/s", systemName,
	    end.body.empty() ? "" : " (" + end.body + ")", jumps.empty() ? "no jumps" : jumps,
	    route->seconds, speed);
}


void SpaceLayer::ShowArrival(SpaceScene::JumpArrival &arrival)
{
	// What the old system scene built goes with the scene's old contents. The simulation did not
	// change, so the snapshots are not taken again: the traffic layer's proxies are made from the
	// same ones, now for the entities in the new system. What was picked was in the old one.
	SceneUnloading();
	m_shell.ClearPick();
	Scene::Scene &scene = m_shell.Scene();
	if (arrival.sceneFile)
		scene.Load(*arrival.sceneFile);
	else
		scene.LoadSystem(arrival.starSystemPath);
	BuildSystemScene(std::move(arrival.starSystem));
	// A jump along a lane took the ship with it (TakeShipAlong), and it comes out of the gate the
	// cameras frame; one straight to a system leaves it behind.
	if (m_piloting && !CanPilot())
		StopPiloting();
	m_shownSystem = arrival.system;
	m_arrivedAt = arrival.arrival;
	MarkTargets();
	if (arrival.arrival.empty())
		return;
	for (const auto [body, celestial] :
	     scene.Registry().view<const Scene::CelestialBodyComponent>().each())
		if (celestial.body == arrival.arrival)
		{
			m_shell.Frame(body);
			return;
		}
}


bool SpaceLayer::BindStation()
{
	if (!m_systemScene)
		return false;
	const Celestial::StarSystem &system = m_systemScene->System();
	const auto station =
	    std::ranges::find(system.bodies, Celestial::BodyKind::Station, &Celestial::Body::kind);
	if (station == system.bodies.end())
		return false;
	const std::uint64_t systemId = Sim::FrameId(system.name);
	if (m_station != entt::null && m_stationSystem == systemId && m_stationBody == station->name)
		return true;
	entt::registry &world = m_sim.Registry();
	const auto bind = [&](entt::entity entity)
	{
		m_station = entity;
		m_stationSystem = systemId;
		m_stationBody = station->name;
		Core::Log(Core::Severity::Info, kSpace, "The ship is bound to {}", m_stationBody);
		return true;
	};

	// Built by the docks to its layout (OpenStations).
	if (const entt::entity built = FindStation(world, systemId, station->name); built != entt::null)
		return bind(built);
	// A station of no layout, made on an earlier visit or in the world a save loaded: a body on the
	// station's rails that is neither a pad nor a ship clamped to one.
	for (const auto [entity, rails, inSystem, body] :
	     world
	         .view<const Physics::OnRails, const Sim::InFrame, const Physics::Body>(
	             entt::exclude<Dock::Hull>)
	         .each())
		if (inSystem.frame == systemId && rails.body == station->name)
			return bind(entity);

	entt::registry &scene = m_shell.Scene().Registry();
	entt::entity shown = entt::null;
	for (const auto [entity, celestial] : scene.view<const Scene::CelestialBodyComponent>().each())
		if (celestial.body == station->name)
			shown = entity;
	if (shown == entt::null)
		return false;

	// The station's hull, as the scene draws it.
	const std::string shape = "stations/" + SpaceScene::BodyEntityId(station->name);
	const std::optional<CollisionMesh> mesh = MakeCollisionMesh(scene, shown);
	if (!mesh)
	{
		Core::Log(
		    Core::Severity::Warning, kSpace, "No station: '{}' is drawn with no shape to meet",
		    station->name);
		return false;
	}
	if (!m_physics.HasShape(shape))
	{
		if (mesh->skippedModels != 0)
			Core::Log(
			    Core::Severity::Warning, kSpace,
			    "Station '{}' has {} models, which its collision mesh leaves out", station->name,
			    mesh->skippedModels);
		m_physics.DefineShape(shape, mesh->shape);
		Core::Log(
		    Core::Severity::Info, kSpace, "Station '{}' collides as {} triangles, with no pads",
		    station->name, mesh->shape.indices.size() / 3);
	}

	// Where the station is now: placed by its rails from the next step, and by the layer for the
	// snapshots taken again now.
	const Physics::OnRails rails{station->name};
	m_station = world.create();
	world.emplace<Sim::Transform>(
	    m_station, m_physics.RailsPlace(systemId, rails, m_sim.Clock().Now()));
	world.emplace<Sim::InFrame>(m_station, systemId);
	world.emplace<Physics::OnRails>(m_station, rails);
	world.emplace<Physics::LocalFrame>(m_station, station->name);
	world.emplace<Physics::Body>(m_station, Physics::Body{Physics::BodyKind::Kinematic, shape});
	m_shell.Resync();
	return bind(m_station);
}


void SpaceLayer::OpenStations()
{
	// Once a run, as the ship is: a load replaces them with the save's.
	entt::registry &world = m_sim.Registry();
	if (!world.view<Dock::Station>().empty())
		return;
	const std::vector<entt::entity> stations = Sample::OpenStations(
	    m_sim, *m_pDocks, m_galaxySystems, [&](entt::entity station, std::string_view body)
	    { m_game.StationOpened(m_sim, *m_pDocks, station, body); });
	std::size_t pads = 0;
	for (const entt::entity station : stations)
		pads += world.get<Dock::Station>(station).pads.size();
	Core::Log(
	    Core::Severity::Info, kSpace, "{} stations open, with {} pads and {} ships parked",
	    stations.size(), pads, world.view<const Dock::Hull>().size());

	m_game.StationsOpened(World(), m_shell.Definitions());
	m_shell.Resync();
}


void SpaceLayer::SpawnShip()
{
	if (m_ship != entt::null || !BindStation())
		return;
	const ShipKind kind = SpaceGame::StarterShip();
	const Sim::Controller player = m_shell.Player();
	entt::registry &world = m_sim.Registry();
	const Sim::Transform at = world.get<Sim::Transform>(m_station);

	// In the station's frame, flown by its pilot.
	m_ship = world.create();
	world.emplace<Sim::Transform>(m_ship, at);
	world.emplace<Sim::InFrame>(m_ship, m_stationSystem);
	world.emplace<Physics::LocalFrame>(m_ship, m_stationBody);
	MakeShip(world, *m_pDocks, m_ship, kind.hull, kind.mass, true);
	world.replace<Physics::FlightControls>(m_ship, Physics::FlightControls{{}, {}, true, m_station});
	world.emplace<Sim::ControlledBy>(m_ship, player);
	// The fleet's first ship, fitted by the game, at the player's helm.
	m_game.FitShip(world, m_ship, player);
	world.emplace<Sim::Helm>(m_ship, player);
	m_shipSystem = m_stationSystem;

	// Docked on the lowest numbered free pad it fits; at a station of none, off its -Z end, nose
	// towards it.
	const auto *station = world.try_get<Dock::Station>(m_station);
	const Dock::HullDefinition *hull = m_pDocks->FindHull(kind.hull);
	const entt::entity pad = station && hull ? FreePad(world, *m_pDocks, m_station, *hull) : entt::null;
	if (pad != entt::null)
	{
		m_pDocks->Park(m_sim, m_ship, pad);
		m_docked = true;
		Core::Log(
		    Core::Severity::Info, kSpace,
		    "The ship is docked on pad {} of {}: undock lifts it clear, and P flies it",
		    world.get<Dock::Pad>(pad).number, m_stationBody);
	}
	else
	{
		// Clear by the ship's own reach as well as the station's: a hull may be any size.
		const double standOff = m_physics.ShapeRadius(world.get<Physics::Body>(m_station).shape) +
		                        m_physics.ShapeRadius(world.get<Physics::Body>(m_ship).shape) +
		                        kShipStandOff;
		world.replace<Sim::Transform>(
		    m_ship, Sim::Transform{
		                at.position + at.orientation * glm::dvec3{0.0, 0.0, -standOff},
		                at.orientation * glm::angleAxis(glm::pi<double>(), glm::dvec3{0.0, 1.0, 0.0})});
		Core::Log(
		    Core::Severity::Info, kSpace, "A ship holds station off {}: P flies it", m_stationBody);
	}
	m_shell.Resync();
}


void SpaceLayer::RequestPad(bool fly)
{
	if (m_ship == entt::null || m_station == entt::null)
	{
		Core::Log(Core::Severity::Warning, kSpace, "No ship, or no station to ask for a pad");
		return;
	}
	m_sim.Commands().Push(Dock::RequestPad{m_shell.Player(), m_ship, m_station, fly});
}


void SpaceLayer::ReleasePad()
{
	if (m_ship != entt::null)
		m_sim.Commands().Push(Dock::ReleasePad{m_shell.Player(), m_ship, false});
}


void SpaceLayer::DrawStations()
{
	m_dressings.clear();
	m_dressedStations.clear();
	if (!m_systemScene)
		return;
	entt::registry &scene = m_shell.Scene().Registry();
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


void SpaceLayer::ShowPadStates()
{
	if (m_dressings.empty() || !m_systemScene)
		return;
	const Sim::Snapshot &snapshot = m_shell.Snapshot();
	entt::registry &scene = m_shell.Scene().Registry();
	const std::uint64_t system = Sim::FrameId(m_systemScene->System().name);
	for (std::size_t index = 0; index < m_dressings.size(); ++index)
	{
		entt::entity &station = m_dressedStations[index];
		if (Sample::ShowPadStates(scene, m_dressings[index], snapshot, station, *m_pDocks) != 0)
			continue;
		// Not yet found, or a world loaded over the one it was found in: this frame's snapshot is the
		// world's latest step, so the station found now is the one its pads name.
		station = FindStation(m_sim.Registry(), system, m_dressings[index].body);
		if (station != entt::null)
			Sample::ShowPadStates(scene, m_dressings[index], snapshot, station, *m_pDocks);
	}
}


void SpaceLayer::SpawnTraffic(int count)
{
	entt::registry &world = m_sim.Registry();
	std::erase_if(m_traffic, [&world](entt::entity entity) { return !world.valid(entity); });
	if (count == 0)
	{
		for (const entt::entity entity : m_traffic)
			world.destroy(entity);
		Core::Log(Core::Severity::Info, kSpace, "{} shuttles taken out of the world", m_traffic.size());
		m_traffic.clear();
		m_shell.Resync();
		return;
	}
	if (!BindStation())
	{
		Core::Log(Core::Severity::Warning, kSpace, "No station in the system shown to send traffic to");
		return;
	}
	const std::size_t room = kMostTraffic - m_traffic.size();
	const std::size_t sent = std::min(static_cast<std::size_t>(count), room);
	if (sent == 0)
	{
		Core::Log(Core::Severity::Warning, kSpace, "Already {} shuttles, the most", kMostTraffic);
		return;
	}
	const ShipKind kind = SpaceGame::StarterShip();
	// The ring is clear of the hull by more than a shuttle's clearance and separation together, and
	// its places, in the station's axes, a sixteenth of it apart: 100 m or so, twice what two
	// shuttles keep between them.
	const Sim::Transform at = world.get<Sim::Transform>(m_station);
	const double ring = m_physics.ShapeRadius(world.get<Physics::Body>(m_station).shape) + 150.0;
	std::vector<bool> taken(kMostTraffic, false);
	for (const entt::entity entity : m_traffic)
		if (const auto *destination = world.try_get<Ai::Destination>(entity))
			for (std::size_t place = 0; place < kMostTraffic; ++place)
				if (glm::distance(destination->offset, TrafficPlace(place, ring)) < 1.0)
					taken[place] = true;
	// Drawn from a generator of the layer's, so sending traffic does not move the world's.
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
		world.emplace<Sim::InFrame>(shuttle, m_stationSystem);
		world.emplace<Physics::LocalFrame>(shuttle, m_stationBody);
		MakeShip(world, *m_pDocks, shuttle, kind.hull, kind.mass, true);
		world.emplace<Ai::Steered>(shuttle);
		Ai::Destination destination;
		destination.entity = m_station;
		destination.offset = TrafficPlace(place, ring);
		destination.speed = kTrafficSpeed;
		world.emplace<Ai::Destination>(shuttle, destination);
		m_traffic.push_back(shuttle);
	}
	std::ranges::sort(m_traffic, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	m_shell.Resync();
	Core::Log(
	    Core::Severity::Info, kSpace, "{} shuttles sent to hold station by {}, {} in all", sent,
	    m_stationBody, m_traffic.size());
}


void SpaceLayer::Follow(entt::entity ship)
{
	const entt::registry &world = m_sim.Registry();
	if (!world.valid(ship) || ship == m_ship)
		return;
	// A camera that followed the ship it leaves follows this one, once it is shown.
	const bool following = m_piloting || m_resumePiloting;
	StopPiloting();
	m_ship = ship;
	m_resumePiloting = following;
	m_sentControls.reset();
	m_shell.ClearPick();
	const auto *inSystem = world.try_get<Sim::InFrame>(ship);
	m_shipSystem = inSystem ? inSystem->frame : Sim::kNoFrame;
	// Bound to the station it is docked at or waiting for; else, once it is shown, to the one of
	// its system (AfterEvents).
	if (const auto *docking = world.try_get<Dock::Docking>(ship))
	{
		m_station = docking->station;
		m_stationSystem = world.get<Sim::InFrame>(m_station).frame;
		m_stationBody = world.get<Physics::OnRails>(m_station).body;
	}
	m_docked = Dock::IsDocked(world, m_ship, m_station);
	const auto *hull = world.try_get<Dock::Hull>(ship);
	const Dock::HullDefinition *definition = hull ? m_pDocks->FindHull(hull->definition) : nullptr;
	Core::Log(
	    Core::Severity::Info, kSpace, "Attached to {}, a {}, {}", m_shell.Describe(ship),
	    definition ? definition->displayName : std::string{"ship"},
	    AtHelm() ? "at your helm" : "which its behaviour flies");
	if (m_resumePiloting && CanPilot())
	{
		m_resumePiloting = false;
		StartPiloting(m_pilotView);
	}

	// Shown in its system: the scene jumps there, or along the lane it is on to where it comes out.
	if (!m_galaxy || m_jump->Active())
		return;
	std::uint64_t system = m_shipSystem;
	std::string arrival;
	if (const auto *lane = world.try_get<Space::OnLane>(ship))
	{
		system = lane->to;
		arrival = lane->arrival;
	}
	const auto shown = std::ranges::find(
	    m_galaxy->systems, system,
	    [](const Celestial::GalaxySystem &entry) { return Sim::FrameId(entry.name); });
	const auto index = static_cast<std::size_t>(shown - m_galaxy->systems.begin());
	if (shown == m_galaxy->systems.end() || (m_shownSystem && *m_shownSystem == index))
		return;
	m_jumpStep = m_sim.Clock().Step();
	m_jump->Begin(*m_galaxy, index, arrival);
}


void SpaceLayer::CycleAttached(int step)
{
	const std::vector<entt::entity> fleet = FleetOf(m_shell.Snapshot(), m_shell.Player());
	if (fleet.empty())
		return;
	const auto count = static_cast<std::ptrdiff_t>(fleet.size());
	const std::ptrdiff_t at = std::ranges::find(fleet, m_ship) - fleet.begin();
	// From a ship not of the fleet, the first, or the last going back.
	const std::ptrdiff_t next =
	    at == count ? (step > 0 ? 0 : count - 1) : ((at + step) % count + count) % count;
	Follow(fleet[static_cast<std::size_t>(next)]);
}


bool SpaceLayer::AtHelm() const
{
	return m_ship != entt::null && HelmOf(m_shell.Snapshot(), m_shell.Player()) == m_ship;
}


void SpaceLayer::ToggleHelm()
{
	if (m_ship == entt::null)
	{
		Core::Log(Core::Severity::Warning, kSpace, "No ship to take the helm of");
		return;
	}
	if (AtHelm())
		m_sim.Commands().Push(Ai::LeaveHelm{m_shell.Player(), m_ship});
	else
		m_sim.Commands().Push(Ai::TakeHelm{m_shell.Player(), m_ship});
}


void SpaceLayer::ListFleet(DevTools::Console &console) const
{
	const Sim::Snapshot &snapshot = m_shell.Snapshot();
	const Sim::Controller player = m_shell.Player();
	const std::vector<entt::entity> fleet = FleetOf(snapshot, player);
	if (fleet.empty())
	{
		console.Print("The fleet has no ships");
		return;
	}
	for (std::size_t number = 0; number < fleet.size(); ++number)
	{
		const entt::entity ship = fleet[number];
		const auto *hull = snapshot.Get<Dock::Hull>(ship);
		const Dock::HullDefinition *definition = hull ? m_pDocks->FindHull(hull->definition) : nullptr;
		std::string where = "in no system, on a lane";
		if (const auto *docking = snapshot.Get<Dock::Docking>(ship))
			where = std::format(
			    "{} {}", docking->stage == Dock::Stage::Docked ? "docked at" : "making for a pad at",
			    m_shell.Describe(docking->station));
		else if (const auto *sample = snapshot.FindSample(ship);
		         sample && sample->frame != Sim::kNoFrame)
			if (const auto *frame = snapshot.Get<Physics::LocalFrame>(ship))
				where = frame->body.empty() ? "cruising" : std::format("off {}", frame->body);
		console.Print(
		    std::format(
		        "{}{}  {}  {}  {}", number + 1, ship == m_ship ? "*" : " ",
		        definition ? definition->displayName : std::string{"?"}, where,
		        m_game.DescribeShip(snapshot, ship, HelmOf(snapshot, player) == ship)));
	}
}


bool SpaceLayer::CanPilot() const
{
	return m_ship != entt::null && m_systemScene &&
	       Sim::FrameId(m_systemScene->System().name) == m_shipSystem;
}


bool SpaceLayer::StartPiloting(Scene::PilotView view)
{
	if (!CanPilot())
	{
		Core::Log(Core::Severity::Warning, kSpace, "No ship to fly in the system shown");
		return false;
	}
	if (m_sim.Clock().Compression() > 1.0)
	{
		Core::Log(Core::Severity::Warning, kSpace, "No piloting under time compression");
		return false;
	}
	m_piloting = true;
	m_pilotView = view;
	SyncContext();
	return true;
}


void SpaceLayer::StopPiloting()
{
	m_piloting = false;
	SyncContext();
}


void SpaceLayer::SyncContext()
{
	m_shell.SetGameContext(m_piloting ? PilotInput::kContext : std::string_view{});
}


void SpaceLayer::BindSequenceRoles(entt::registry &registry)
{
	// What the layer binds, of what a sequence may name, each frame, since a proxy is made by the
	// sync and a pad's clamps are shown closed from a snapshot: a sequence that names another role is
	// refused, and one whose role is not shown yet waits for it.
	const entt::entity ship =
	    m_ship == entt::null ? entt::entity{entt::null} : ProxyOf(registry, m_ship);
	entt::entity pad = entt::null;
	entt::entity station = entt::null;
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


void SpaceLayer::FlyShip()
{
	// A ship whose behaviour flies it, or another's, is followed, not flown: the step would refuse.
	if (m_ship == entt::null || !AtHelm())
		return;
	// A frame's input is a minute of flight at 60x: piloting is suspended under compression, as it is
	// in every game of the genre, and the assist holds the ship.
	if (m_piloting && m_sim.Clock().Compression() > 1.0)
		StopPiloting();
	Physics::FlightControls controls;
	if (m_piloting)
		controls = m_shell.SmokeTest() ? kSmokeFlight : m_pilotInput->Read(m_shell.Input());
	// Let go, the ship holds station on its assist, whatever the pilot left it on.
	controls.assist = m_piloting ? m_assist : true;
	// Against the station while the ship is in its frame; against its frame elsewhere, on the way
	// to the next system or off another body after a cruise.
	controls.reference =
	    m_stationSystem == m_shipSystem && ShipFrame() == m_stationBody ? m_station : entt::null;
	if (m_sentControls == controls)
		return;
	m_sim.Commands().Push(Physics::SetFlightControls{m_shell.Player(), m_ship, controls});
	m_sentControls = controls;
}


void SpaceLayer::SyncPilotCamera(entt::registry &registry)
{
	const entt::entity camera = m_shell.ActiveCamera();
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
	const entt::entity proxy = ProxyOf(registry, m_ship);
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


void SpaceLayer::ShowJumpStreaks()
{
	entt::registry &registry = m_shell.Scene().Registry();
	// The scene a jump arrives in is another, and the streaks the old one's.
	if (!registry.valid(m_jumpStreaks))
		m_jumpStreaks = entt::null;
	const SpaceScene::JumpStage stage = m_jump->Stage();
	const bool streaking =
	    stage == SpaceScene::JumpStage::FadingOut || stage == SpaceScene::JumpStage::FadingIn;
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


void SpaceLayer::PlaceJumpStreaks(entt::registry &registry)
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


void SpaceLayer::HumStation(entt::registry &registry)
{
	// A jump's new system scene makes a new one, which hums once it is there.
	if (m_stationBody.empty())
		return;
	for (const auto [entity, celestial] : registry.view<const Scene::CelestialBodyComponent>().each())
		if (celestial.body == m_stationBody && !registry.all_of<Audio::SoundComponent>(entity))
		{
			Audio::SoundComponent hum;
			hum.sound = kStationHum;
			hum.volume = kStationHumVolume;
			hum.falloff = kStationHumFalloff;
			registry.emplace<Audio::SoundComponent>(entity, std::move(hum));
		}
}


void SpaceLayer::MaskCockpit(entt::registry &registry)
{
	if (m_masked != entt::null && registry.valid(m_masked))
		registry.remove<Scene::MaskComponent>(m_masked);
	m_masked = entt::null;
	if (!m_piloting || m_pilotView != Scene::PilotView::Cockpit)
		return;
	// A mask the scene file authored is its own, and stays.
	if (const entt::entity proxy = ProxyOf(registry, m_ship);
	    proxy != entt::null && !registry.all_of<Scene::MaskComponent>(proxy))
	{
		registry.emplace<Scene::MaskComponent>(proxy, Scene::MaskComponent{.cockpit = true});
		m_masked = proxy;
	}
}


void SpaceLayer::SetProxyFactory()
{
	m_systemScene->SetProxyFactory(
	    [this](entt::registry &registry, entt::entity simEntity)
	    {
		    const Sim::Snapshot &snapshot = m_shell.Snapshot();
		    if (snapshot.Get<Dock::Buggy>(simEntity))
			    return MakeBuggyProxy(registry, simEntity);
		    // Every ship as its hull, the fleet's, the traders' and the traffic's; a mod without the
		    // hull still sees it, as a box.
		    const auto *hull = snapshot.Get<Dock::Hull>(simEntity);
		    if (!hull)
			    return entt::entity{entt::null};
		    const Dock::HullDefinition *definition = m_pDocks->FindHull(hull->definition);
		    return MakeHullProxy(
		        registry, definition ? *definition : Dock::HullDefinition{}, simEntity);
	    });
}


bool SpaceLayer::SyncProxies()
{
	// A system's scene is the traffic layer's, with proxies made and unmade; a scene of no system is
	// the shell's to sync.
	if (!m_systemScene)
		return false;
	m_systemScene->SyncTraffic(m_shell.Previous(), m_shell.Snapshot(), m_shell.Alpha());
	return true;
}


void SpaceLayer::DrawFlight()
{
	// A cinematic has the frame to itself.
	if (!m_shell.Scene().Registry().view<Scene::SequencePlayerComponent>().empty())
		return;
	// Speeds from the snapshots, which are what presentation may see: the ship's motion relative to
	// the station's over the last step.
	const Sim::Snapshot &previous = m_shell.Previous();
	const Sim::Snapshot &current = m_shell.Snapshot();
	const auto *shipSample = current.FindSample(m_ship);
	const auto *stationSample = current.FindSample(m_station);
	// A station a system away, as it is until the ship's arrival binds it to the next, is no measure.
	if (!shipSample || !stationSample || shipSample->frame != stationSample->frame)
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
	    Sim::FrameId(m_systemScene->System().name) == shipSample->frame)
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
		const bool helm = AtHelm();
		ImGui::Text(
		    "%s  %s", m_shell.Describe(m_ship).c_str(),
		    helm ? "at your helm" : "flown by its behaviour (H takes the helm)");
		if (const auto *cruising = current.Get<Space::Cruising>(m_ship))
			ImGui::Text(
			    "Cruising to %s  %.0f km/s  %.0f s", cruising->target.c_str(), speed / 1000.0,
			    static_cast<double>(
			        cruising->arrivalStep - std::min(cruising->arrivalStep, current.step)) *
			        m_sim.FixedStep());
		ImGui::Text(
		    "%s  %.1f m/s  %.0f m", against.c_str(), speed, glm::length(ship->position - reference));
		if (m_docked)
			ImGui::Text("Docked at %s", m_stationBody.c_str());
		if (const auto *transit = current.Get<Space::Transit>(m_ship))
			if (const auto *gate = current.Get<Space::Gate>(transit->gate))
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
		    "Assist %s (Z)  View %s (V)  P lets go  [ ] other ships", m_assist ? "on" : "off",
		    m_pilotView == Scene::PilotView::Chase ? "chase" : "cockpit");
		if (helm)
			ImGui::TextDisabled("W/S throttle  A/D R/F strafe  arrows pitch, yaw  Q/E roll");
	}
	ImGui::End();
}


void SpaceLayer::TakeEvent(const Sim::SimEvent &event)
{
	if (const auto *entered = event.event.try_cast<Space::LaneEntered>();
	    entered && entered->entity == m_ship)
	{
		m_shipSystem = Sim::kNoFrame;
		m_resumePiloting = m_piloting;
		if (m_piloting)
			StopPiloting();
		Core::Log(
		    Core::Severity::Info, kSpace, "The ship entered the lane through {}, out at {} in {:.0f} s",
		    entered->lane.departure, entered->lane.arrival, entered->transitSeconds);
		// Sent by a gate rather than the tools' jump, which has the scene jumping already.
		FollowShipAlong(entered->lane);
	}
	else if (const auto *cleared = event.event.try_cast<Space::TransitCleared>();
	         cleared && cleared->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace,
		    "{} clears the ship through for {} credits: fly through its ring within {:.0f} s",
		    m_shell.Describe(cleared->gate), cleared->toll, Space::Gates::kClearanceSeconds);
	else if (const auto *waiting = event.event.try_cast<Space::TransitQueued>();
	         waiting && waiting->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace, "The ship waits for {}, {} in the queue",
		    m_shell.Describe(waiting->gate), waiting->place);
	else if (const auto *noTransit = event.event.try_cast<Space::TransitRefused>();
	         noTransit && noTransit->ship == m_ship)
		Core::Log(
		    Core::Severity::Warning, kSpace, "{} will not clear the ship: {}",
		    m_shell.Describe(noTransit->gate), m_shell.Word(noTransit->reason));
	else if (const auto *unused = event.event.try_cast<Space::TransitLapsed>();
	         unused && unused->ship == m_ship)
		Core::Log(
		    Core::Severity::Warning, kSpace, "The ship's clearance through {} has lapsed",
		    m_shell.Describe(unused->gate));
	else if (const auto *sent = event.event.try_cast<Space::TransitSent>();
	         sent && sent->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace, "{} sends the ship through, charging {} credits",
		    m_shell.Describe(sent->gate), sent->toll);
	else if (const auto *passed = event.event.try_cast<Space::GatePassed>();
	         passed && passed->ship == m_ship)
		Core::Log(
		    Core::Severity::Warning, kSpace, "The ship passed through {} unsent: {}",
		    m_shell.Describe(passed->gate), m_shell.Word(passed->reason));
	else if (const auto *arrived = event.event.try_cast<Space::LaneLeft>();
	         arrived && arrived->entity == m_ship)
	{
		m_shipSystem = arrived->system;
		Core::Log(Core::Severity::Info, kSpace, "The ship came out at {}", arrived->arrival);
	}
	else if (const auto *cruise = event.event.try_cast<Space::CruiseStarted>();
	         cruise && cruise->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace, "The ship cruises to {}, {:.0f} km in {:.1f} s", cruise->body,
		    cruise->metres / 1000.0, cruise->seconds);
	else if (const auto *cruised = event.event.try_cast<Space::CruiseEnded>();
	         cruised && cruised->ship == m_ship)
		Core::Log(Core::Severity::Info, kSpace, "The ship has arrived off {}", cruised->body);
	else if (const auto *noCruise = event.event.try_cast<Space::CruiseRefused>();
	         noCruise && noCruise->ship == m_ship)
		Core::Log(
		    Core::Severity::Warning, kSpace, "The ship cannot cruise to {}: {}", noCruise->body,
		    m_shell.Word(noCruise->reason));
	else if (const auto *noLane = event.event.try_cast<Space::LaneRefused>();
	         noLane && noLane->entity == m_ship)
		Core::Log(
		    Core::Severity::Warning, kSpace, "The ship cannot enter the lane through {}: {}",
		    noLane->jumpPoint, m_shell.Word(noLane->reason));
	// The console's traffic holds station where it arrives; a trader arriving is on its way on.
	else if (const auto *holding = event.event.try_cast<Ai::Arrived>();
	         holding && std::ranges::binary_search(
	                        m_traffic, entt::to_integral(holding->ship), {},
	                        [](entt::entity entity) { return entt::to_integral(entity); }))
		Core::Log(
		    Core::Severity::Info, kSpace, "{} holds station by {}", m_shell.Describe(holding->ship),
		    m_stationBody);
	else if (const auto *landed = event.event.try_cast<Dock::Landed>();
	         landed && landed->ship == m_ship)
	{
		m_docked = true;
		Core::Log(
		    Core::Severity::Info, kSpace, "The ship is clamped to pad {} of {}", landed->number,
		    m_stationBody);
		m_shell.PlaySequence(kDockSequence, 0.0);
	}
	else if (const auto *left = event.event.try_cast<Dock::Left>(); left && left->ship == m_ship)
	{
		m_docked = false;
		Core::Log(
		    Core::Severity::Info, kSpace, "The clamps of pad {} of {} let the ship go", left->number,
		    m_stationBody);
	}
	else if (const auto *given = event.event.try_cast<Dock::PadGiven>(); given && given->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace, "{} gives the ship pad {}", m_stationBody, given->number);
	else if (const auto *queued = event.event.try_cast<Dock::PadQueued>();
	         queued && queued->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace, "The ship waits for a pad at {}, {} in the queue",
		    m_stationBody, queued->place);
	else if (const auto *noPad = event.event.try_cast<Dock::PadRefused>();
	         noPad && noPad->ship == m_ship)
		Core::Log(Core::Severity::Warning, kSpace, "No pad: {}", m_shell.Word(noPad->reason));
	else if (const auto *lapsed = event.event.try_cast<Dock::PadLapsed>();
	         lapsed && lapsed->ship == m_ship)
		Core::Log(
		    Core::Severity::Warning, kSpace, "Pad {} was not landed on in time, and is given up",
		    lapsed->number);
	else if (const auto *carried = event.event.try_cast<Dock::CargoCarried>();
	         carried && carried->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace, "The ship's cargo has crossed the deck of pad {} of {}",
		    carried->number, m_stationBody);
	else if (const auto *released = event.event.try_cast<Dock::PadReleased>();
	         released && released->ship == m_ship)
	{
		// Sent along a lane from its pad, the clamps let it go without a Left.
		m_docked = false;
		// The docks held it as they gave the pad up: the pilot's controls go again, held or not.
		m_sentControls.reset();
		Core::Log(Core::Severity::Info, kSpace, "The ship gives up its pad at {}", m_stationBody);
	}
	else if (const auto *helmTaken = event.event.try_cast<Ai::HelmTaken>();
	         helmTaken && helmTaken->controller == m_shell.Player())
	{
		// Its controls were let go as the helm was taken: the pilot's go again.
		m_sentControls.reset();
		Core::Log(Core::Severity::Info, kSpace, "At the helm of {}", m_shell.Describe(helmTaken->ship));
	}
	else if (const auto *helmLeft = event.event.try_cast<Ai::HelmLeft>();
	         helmLeft && helmLeft->controller == m_shell.Player())
		Core::Log(
		    Core::Severity::Info, kSpace, "{} is left to its behaviour",
		    m_shell.Describe(helmLeft->ship));
	else if (const auto *noHelm = event.event.try_cast<Ai::HelmRefused>();
	         noHelm && noHelm->sender == m_shell.Player())
		Core::Log(
		    Core::Severity::Warning, kSpace, "No helm of {}: {}", m_shell.Describe(noHelm->ship),
		    m_shell.Word(noHelm->reason));
	else if (const auto *unflown = event.event.try_cast<Physics::FlightControlsRefused>();
	         unflown && unflown->sender == m_shell.Player() && unflown->ship == m_ship)
		Core::Log(
		    Core::Severity::Info, kSpace, "{} does not answer the controls: {}",
		    m_shell.Describe(unflown->ship), m_shell.Word(unflown->reason));
}


void SpaceLayer::AfterEvents()
{
	if (CanPilot() && m_stationSystem != m_shipSystem)
		BindStation();
	if (m_resumePiloting && CanPilot())
	{
		m_resumePiloting = false;
		StartPiloting(m_pilotView);
	}
}


void SpaceLayer::ShuttingDown() noexcept
{
	// The system scene holds the scene, and the jump the handles of a destination's files while it
	// reads them.
	m_systemScene.reset();
	m_jump.reset();
}


std::optional<std::string> SpaceLayer::Describe(entt::entity entity) const
{
	const std::vector<entt::entity> fleet = FleetOf(m_shell.Snapshot(), m_shell.Player());
	if (const auto found = std::ranges::find(fleet, entity); found != fleet.end())
		return std::format("Ship {}", found - fleet.begin() + 1);
	if (entity == m_ship)
		return "Ship";
	if (entity == m_station)
		return m_stationBody;
	if (const auto found = std::ranges::find(m_traffic, entity); found != m_traffic.end())
		return std::format("Shuttle {}", found - m_traffic.begin() + 1);
	const Sim::Snapshot &snapshot = m_shell.Snapshot();
	if (const auto *gate = snapshot.Get<Space::Gate>(entity))
		return gate->jumpPoint;
	// Another station, by the body it rides: not a pad, nor a ship clamped to one, which ride it too.
	if (const auto *rails = snapshot.Get<Physics::OnRails>(entity);
	    rails && !snapshot.Get<Dock::Pad>(entity) && !snapshot.Get<Dock::Hull>(entity))
		return rails->body;
	return std::nullopt;
}


std::optional<App::PickSpace> SpaceLayer::PickIn() const
{
	if (!m_systemScene)
		return std::nullopt;
	// From the seat, the ray starts inside the hull; while flying, what is picked is the ship's target.
	return App::PickSpace{
	    Sim::FrameId(m_systemScene->System().name),
	    m_piloting && m_pilotView == Scene::PilotView::Cockpit ? m_ship : entt::entity{entt::null},
	    m_piloting};
}


glm::dvec3 SpaceLayer::PickOrigin(double simSeconds) const
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


std::string SpaceLayer::ShownSystem() const
{
	return m_systemScene ? m_systemScene->System().name : std::string{};
}


std::string SpaceLayer::ShipFrame() const
{
	const auto *frame =
	    m_ship == entt::null ? nullptr : m_shell.Snapshot().Get<Physics::LocalFrame>(m_ship);
	return frame ? frame->body : std::string{};
}


void SpaceLayer::DriveSmoke(App::SmokeActions &actions)
{
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
	actions.jump = [this] { return Jump(); };
	actions.jumping = [this] { return m_jump->Active(); };
}
} // namespace Jettison::Sample
