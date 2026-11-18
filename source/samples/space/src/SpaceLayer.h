#pragma once

#include <entt/entt.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/app/Game.h>
#include <jettison/celestial/Galaxy.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/input/Actions.h>
#include <jettison/physics/Flight.h>
#include <jettison/scene/Components.h>
#include <jettison/spacescene/JumpTransition.h>
#include <jettison/spacescene/SystemScene.h>

#include "Dressing.h"
#include "Piloting.h"
#include "ShellState.h"

namespace Jettison::Ai
{
class Behaviours;
}
namespace Jettison::Space
{
class CruiseTravel;
class Gates;
class LaneTravel;
struct OnLane;
} // namespace Jettison::Space
namespace Jettison::Sample
{
class SpaceGame;
struct SpaceWorld;

// The space game's place in the shell (docs/sample-game.md): the galaxy and its star systems, the
// system shown and the jumps between them (SpaceScene), lanes, gates, cruises and routes; the
// stations built to their layouts and dressed in the scene; the player's ship and fleet, piloted from
// behind or from its seat, and the console's traffic. The shell had all of it until G01, and is a
// game's host now; SpaceGame composes this into it, through its hooks, and its own trading on top.
//
// The ship is docked on a pad of the first station of the system it opens on, which the console's
// undock lifts clear and dock flies back in, which P flies and V looks at from behind or from its
// seat, and which a jump along a lane takes with it (Space::LaneTravel) when it is in the system
// shown.
class SpaceLayer
{
  public:
	// Attaches the space simulation to the world, after physics: lanes, gates, cruises, the spatial
	// index, steering, the docks and the behaviour trees, in the order the shell attached them, which
	// is the order their systems step in; and the layer's input actions.
	SpaceLayer(SpaceGame &game, const App::ShellWorld &world, App::Shell &shell);

	~SpaceLayer();

	SpaceLayer(const SpaceLayer &) = delete;

	SpaceLayer &operator=(const SpaceLayer &) = delete;

	// The docks' definitions, read before the mods' scripts load.
	static void LoadDefinitions(Sim::DefinitionRegistry &definitions);

	// The docks defined; then the galaxy and its systems read and defined in the simulation, what the
	// game runs across them (SpaceGame::OpenGalaxy), and the jump between them made.
	void Define(const Sim::DefinitionRegistry &definitions);

	void OpenGalaxy(const Sim::DefinitionRegistry &definitions);

	void AddCommands(DevTools::Console &console);

	void RegisterFrame(Ecs::Scheduler &frame);

	// The scene's star system built, and the cameras' targets marked; the first time, the stations, the
	// gates and the ship opened as well.
	void SceneLoaded();

	void SceneUnloading();

	void WriteSave(Sim::SaveGame &save) const;

	// Refuses a save of another galaxy, and keeps its shell section for Loaded.
	void ReadSave(const Sim::SaveGame &save);

	void Loaded();

	void PollInput(const Input::InputSystem &input);

	bool Back();

	void DrawPanels(bool devTools);

	void TakeEvent(const Sim::SimEvent &event);

	bool SyncProxies();

	void ShuttingDown() noexcept;

	std::optional<std::string> Describe(entt::entity entity) const;

	std::optional<App::PickSpace> PickIn() const;

	// The origin of the ship's frame at a time, in the system frame: where its body is, or the
	// primary when there is none.
	glm::dvec3 PickOrigin(double simSeconds) const;

	void DriveSmoke(App::SmokeActions &actions);

	// The attached ship of the player's fleet and the station it holds station by, as simulation
	// entities, null until a station is found to put them at; whether the ship is docked there, as
	// the last event said; and the star system the scene shows, or empty.
	entt::entity Ship() const
	{
		return m_ship;
	}

	entt::entity Station() const
	{
		return m_station;
	}

	bool Docked() const
	{
		return m_docked;
	}

	std::string ShownSystem() const;

	// The fleet (FleetOf): the ship followed, the attached one, which its camera, the game's screens
	// and the console's commands are for; bound to the station it is docked at or waiting for, and
	// shown in its system, the scene jumping there, or along the lane it is on.
	void Follow(entt::entity ship);

	// The space simulation on the world, as the game's own is built on it.
	SpaceWorld World() const;

	// The galaxy's systems, in its order, as their files have them.
	std::span<const Celestial::StarSystem> Systems() const
	{
		return m_galaxySystems;
	}

  private:
	void InitGalaxy();

	// Every system the galaxy has defined in physics, for rails and gravity, and in lane travel, for
	// where a ship comes out.
	void DefineStarSystems();

	// The scene's star system, its proxies and its stations, built on the scene loaded.
	void BuildSystemScene(Celestial::StarSystem system);

	// Which of the galaxy's systems the scene shows, checked against the galaxy's lanes.
	void FindShownSystem();

	// Each celestial body of the scene marked as a target of the orbit camera (App::OrbitTarget).
	void MarkTargets();

	// Starts a jump: to the system of that name, along the lane from the system shown if one leads
	// there and straight there if none does; or, with no name, out through the exit after the one
	// the last jump arrived by, so jumping again and again tours the galaxy's lanes. A scene of none
	// of the galaxy's systems jumps to its first. False, saying why, if no jump started.
	bool Jump(std::string_view systemName = {});

	// The jump's swap, with its effect covering the frame: the scene replaced with the destination's,
	// its system scene built, and the cameras on the jump point arrived at.
	void ShowArrival(SpaceScene::JumpArrival &arrival);

	void DrawGalaxy();

	// The ship bound to the first station of the system shown, the one it docks at and trades with:
	// the station the docks built to its layout (OpenStations); or, for a station of no layout, its
	// body, kinematic on its rails with a collision mesh from what the scene draws it with
	// (MakeCollisionMesh), made the first time the system is shown with the ship in it and found
	// again in the world after that, which has no pads. False for a system with no station, or one
	// of no layout drawn with no shape.
	bool BindStation();

	// Every station of the galaxy built to a layout, and what the game puts at them
	// (SpaceGame::StationOpened, StationsOpened); once a run, and a load brings its own.
	void OpenStations();

	// The station bound, and the ship docked on its lowest numbered free pad, or, at a station of no
	// pads, holding off it in its frame. Once a run: a reload of the scene leaves the simulation as
	// it was.
	void SpawnShip();

	// The console's dock and undock: a pad asked of the bound station for the ship, flown in unless
	// the pilot is to land it, and the pad given up, which lifts it clear if it is docked.
	void RequestPad(bool fly);

	void ReleasePad();

	// The stations of the system shown dressed under their entities in the scene (DressStation), as
	// each system scene is built.
	void DrawStations();

	// The presentation's sync, after the traffic layer's: each dressed station's lamps turned to its
	// pads' states in the latest snapshot, its simulation entity found again when it has none there.
	void ShowPadStates();

	// The console's traffic: that many shuttles flown by steering (Ai::Steering) from 1 to 2 km about
	// the bound station to places on a ring about it, clear of its hull and of each other, in the
	// station's axes so the ring turns with it; or, for none, every one there is taken out of the
	// world. Sixteen at most, the ring's places for its spacing.
	void SpawnTraffic(int count);

	// Sends the ship along the lane, when it is in the system shown, as the scene jumps along it: the
	// tools' jump, which skips the gate and its toll.
	void TakeShipAlong(const Celestial::Exit &exit);

	// The console's transit: asks the gate of the jump point named in the ship's system, or of the
	// one whose frame the ship is in, to clear the ship through it (Space::RequestTransit), found in
	// the snapshots.
	void RequestTransit(std::string_view jumpPoint);

	// The scene's jump along the lane a gate sent the ship down, from the system shown, unless a jump
	// is under way already.
	void FollowShipAlong(const Space::OnLane &lane);

	// The console's route: the quickest way from the ship to a system, at the ship's top speed
	// across each system (Space::PlotRoute, Space::CrossingByCourse).
	void LogRoute(std::string_view systemName) const;

	void CycleAttached(int step);

	// Whether the player is at the attached ship's helm, as the newest snapshot has it: the only ship
	// their controls fly.
	bool AtHelm() const;

	void ToggleHelm();

	void ListFleet(DevTools::Console &console) const;

	// Whether the ship is in the system shown, which is the only one a camera can follow it in.
	bool CanPilot() const;

	bool StartPiloting(Scene::PilotView view);

	void StopPiloting();

	// The flight context pushed while a ship is flown, so the keys that turn the orbit camera fly it.
	void SyncContext();

	// The roles a sequence names that the layer binds, this frame: the ship, the pad it is clamped to,
	// and its station.
	void BindSequenceRoles(entt::registry &registry);

	// The commands phase: the controls the flight context reads, as a SetFlightControls when they
	// change. A ship not flown is held where it is by its assist.
	void FlyShip();

	// The presentation phase, before the rigs: the active camera given a pilot rig on the ship's
	// proxy while it is flown, and its orbit rig back, eased, when it is not.
	void SyncPilotCamera(entt::registry &registry);

	void SetProxyFactory();

	// A jump's streaks past the camera while its effect rises and lifts (an emitter of the scene's,
	// built and not saved), and none once it is done.
	void ShowJumpStreaks();

	// The streaks' emitter ahead of this frame's camera, once the rigs have moved it.
	void PlaceJumpStreaks(entt::registry &registry);

	// The hum of the station the ship is bound to, from wherever the system scene put it.
	void HumStation(entt::registry &registry);

	// From the seat, the ship's own hull is the cockpit, which an outline is not drawn over: its proxy
	// masked each frame, and the mask the layer placed last frame taken off.
	void MaskCockpit(entt::registry &registry);

	void DrawFlight();

	// The events of the steps, after each one is taken: out of a lane into the system shown, the ship
	// is bound to the station there, and flown again if it was flown in.
	void AfterEvents();

	// The frame the ship's physics is in (Physics::LocalFrame), from the newest snapshot: the
	// station's, another body's after a cruise, or none, the system's, while it cruises.
	std::string ShipFrame() const;

	SpaceGame &m_game;
	App::Shell &m_shell;
	Sim::SimWorld &m_sim;
	Physics::PhysicsWorld &m_physics;
	// The world holds them.
	Space::LaneTravel *m_pLanes{nullptr};
	Space::Gates *m_pGates{nullptr};
	Space::CruiseTravel *m_pCruises{nullptr};
	Dock::Docks *m_pDocks{nullptr};
	Ai::Behaviours *m_pBehaviours{nullptr};

	entt::entity m_ship{entt::null};
	entt::entity m_station{entt::null};
	bool m_docked{false};
	// The system the ship is in, none while it is on a lane; and the system and body of the station
	// it is bound to, which after a jump is a system away until the ship's arrival is shown.
	std::uint64_t m_shipSystem{0};
	std::uint64_t m_stationSystem{0};
	std::string m_stationBody;
	// Whether the ship was flown when it entered a lane, so it is flown again once it is shown out of
	// the far end.
	bool m_resumePiloting{false};
	bool m_piloting{false};
	Scene::PilotView m_pilotView{Scene::PilotView::Chase};
	bool m_assist{true};
	// What was last sent, so a command goes only when a control moves.
	std::optional<Physics::FlightControls> m_sentControls;

	// J: a jump, out through the next of the shown system's lanes; P: fly the ship, or follow the one
	// its behaviour flies, and P again to let it go; ] and [: attach to the next ship of the fleet, or
	// the one before; H: its helm, taken or left.
	Input::ActionId m_jumpNext;
	Input::ActionId m_pilot;
	Input::ActionId m_shipNext;
	Input::ActionId m_shipPrevious;
	Input::ActionId m_helm;
	std::optional<PilotInput> m_pilotInput;

	// The scene of the star system the scene names, when it names one: its bodies placed on their
	// orbits, the rest of what the system has built around the file's entities, and the traffic
	// layer. Built on each load, after the scene, and released before it.
	std::optional<SpaceScene::SystemScene> m_systemScene;
	// The galaxy a jump goes between; empty when the assets have none, and then nothing jumps. Its
	// file's virtual path is what a save names it by.
	std::optional<Celestial::Galaxy> m_galaxy;
	std::string m_galaxyPath;
	std::vector<Celestial::StarSystem> m_galaxySystems;
	// The system shown's stations as they are dressed, and each one's simulation entity, found from
	// its celestial body (FindStation) once its pads are in the snapshots.
	std::vector<StationDressing> m_dressings;
	std::vector<entt::entity> m_dressedStations;
	// The traffic spawned (SpawnTraffic), in entity order, which the proxy factory draws as
	// shuttles; found again in the world a save loads, as its steered ships.
	std::vector<entt::entity> m_traffic;
	// The galaxy's system the scene shows, if it shows one, and the jump point the last jump arrived
	// at in it.
	std::optional<std::size_t> m_shownSystem;
	std::string m_arrivedAt;
	// A jump from the system shown to another (docs/architecture.md, "Jumps"): made with the resource
	// manager, which reads the destination's files, and released before it (ShuttingDown). The
	// simulation steps through it as through any other frame; the step it began at is for its report.
	std::optional<SpaceScene::JumpTransition> m_jump;
	std::uint64_t m_jumpStep{0};
	bool m_jumpWasActive{false};
	// The emitter of a jump's streaks while its effect rises and lifts, in the scene shown.
	entt::entity m_jumpStreaks{entt::null};
	// The proxy the cockpit's mask is on, which the layer placed.
	entt::entity m_masked{entt::null};
	// The stations, the gates and the ship opened, once a run.
	bool m_opened{false};
	// The shell section of the save being loaded, from ReadSave to Loaded.
	std::optional<ShellState> m_loading;
};
} // namespace Jettison::Sample
