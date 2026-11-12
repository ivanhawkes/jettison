#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <jettison/app/AppArguments.h>
#include <jettison/app/SmokeSequence.h>
#include <jettison/celestial/Galaxy.h>
#include <jettison/devtools/Console.h>
#include <jettison/devtools/Views.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/input/Actions.h>
#include <jettison/physics/Flight.h>
#include <jettison/platform/Paths.h>
#include <jettison/render/Clusters.h>
#include <jettison/render/DebugDraw.h>
#include <jettison/render/FrameStats.h>
#include <jettison/scene/JumpTransition.h>
#include <jettison/scene/SceneSystems.h>
#include <jettison/scene/SystemScene.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Stepper.h>
#include <jettison/time/Clock.h>
#include <jettison/ui/UiSystem.h>
#include <jettison/vfs/FileSystem.h>

#include "GlfwInput.h"
#include "MarketScreen.h"
#include "Piloting.h"
#include "Saves.h"
#include "Settings.h"

namespace Jettison::Jobs
{
class IoWorker;
}
namespace Jettison::Resource
{
class ResourceManager;
}
namespace Jettison::Render
{
class IRenderer;
}
namespace Jettison::Scene
{
class Scene;
}
namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::App
{

// The app shell: what the engine is when it runs. It owns the subsystems, opens the window, runs
// the frame loop, and owns the teardown.
//
// The lifecycle is explicit and total. Init() builds the subsystems in stages that each have a
// name, so a start-up that fails says which stage it failed in; RunFrameLoop() is the frame loop;
// Shutdown() releases in the reverse order, from the destructor as well as from the loop. Nothing
// in this tree has a destructor of its own - every Vulkan object is released by a Destroy() call -
// so that destructor is the whole safety net. Before this shell, an exception thrown from a frame
// unwound past every one of them: the process exited with the logical device, the ImGui context
// and GLFW live, which is the run the validation layer and the sanitizers exist to be read from.
//
// The state machine is private and exists for one reason: Shutdown() is guarded by it, so the
// loop and ~Application can both ask for teardown and the second one is free.
class Application
{
  public:
	Application(AppArguments arguments, std::string windowTitle);
	~Application();

	Application(const Application &) = delete;
	Application &operator=(const Application &) = delete;

	// Init, the frame loop, Shutdown, then the headless sequence's verdict. A sequence that did
	// not walk its stages throws: it is the render test's gate, and a run that got there by
	// skipping a stage has to fail. Teardown has run by the time this throws, and ~Application
	// covers the paths that throw earlier.
	void Run();

  private:
	// Where the shell is in its lifecycle.
	enum class State
	{
		Uninitialised,
		Initialising,
		Ready,
		Running,
		Stopping,
		Stopped
	};

	// The stages of Init(), in dependency order: the resource manager loads the scene's rigs and
	// the renderer's models, materials and textures, and the scene needs no device, so it loads
	// before the renderer opens the window.
	void Init();
	// Where the user's files are (AppArguments::userDirectory, or the XDG directories), and the
	// settings and bindings read from them.
	void InitUser();
	void InitResources();
	void InitScene();
	// Reads the galaxy (AppArguments::galaxyPath), when there is one to read.
	void InitGalaxy();
	// Loads the scene, and the star system it names; the smoke sequence's reload comes here too.
	void LoadScene();
	// Which of the galaxy's systems the scene shows, checked against the galaxy's lanes.
	void FindShownSystem();
	void InitRenderer();
	// The window the renderer opened: its title and its size.
	void InitWindow();
	// The game UI over the window (docs/architecture.md, "UI"): its fonts, the market screen's data
	// model and document, read through the asset tree.
	void InitUi();

	// The frame's systems, one or two to a phase; registered in the constructor, so a mistake in
	// the order is a start-up error, before a window opens.
	void RegisterFrameSystems();

	// The frame loop, and one frame of it: the scheduler's phases in order (Ecs::kTickPhases).
	void RunFrameLoop();
	void Tick();
	void PollInput();
	// The frame's camera input, into the scene's orbit rigs.
	void DriveCameras(entt::registry &registry, double deltaTime);
	// Moves each orbit camera to the next celestial body, with a transition.
	void NextCameraTarget(entt::registry &registry);
	// Points the orbit camera at the body's entity, out to three of its radii, eased from wherever
	// the camera is.
	void FrameBody(entt::registry &registry, entt::entity camera, entt::entity body);

	// Starts a jump: to the system of that name, along the lane from the system shown if one leads
	// there and straight there if none does; or, with no name, out through the exit after the one
	// the last jump arrived by, so jumping again and again tours the galaxy's lanes. A scene of none
	// of the galaxy's systems jumps to its first. False, saying why, if no jump started.
	bool Jump(std::string_view systemName = {});
	// The jump's swap, with its effect covering the frame: the scene replaced with the destination's,
	// its system scene built, and the cameras on the jump point arrived at.
	void ShowArrival(Scene::JumpArrival &arrival);
	void DrawUI();
	// The presentation phase: the market screen made again from the snapshot, when the step or what
	// the shell shows moved, and the game UI laid out and drawn into its overlay.
	void UpdateGameUi();
	// The window's events since the last frame, to the game UI, unless the dev tools have the mouse or
	// the keyboard.
	void FeedGameUi();
	// The market the screen shows: the one at the ship's station, else the first in the system shown,
	// else none.
	entt::entity ShownMarket() const;
	void ShowMarket(bool shown);
	// The saved-games screen, its list read again from the user mount as it is shown.
	void ShowSaves(bool shown);
	// A Trade the next step applies, from the screen's buttons: crates bought, or sold when negative.
	void SendTrade(std::string_view good, std::int64_t crates);
	// The renderer's counts and the GPU's time in each pass, in the dev tools.
	void DrawRendererPanel();
	// The star system shown, and the scene's layers, each shown or hidden.
	void DrawScenePanel();
	// The galaxy's half of it: the jump under way, and a button for each way out of the system shown.
	void DrawGalaxy();
	// The benchmark's frame count, the warm-up's end, and the report when it is done.
	void AdvanceBenchmark();
	void ReportBenchmark() const;
	// Pushes and pops the flight and console input contexts to match whether a ship is flown and
	// whether the console is open: the console's over the flight's, over the shell's.
	void SyncContexts();

	// Piloting (docs/architecture.md, "Physics"). The shell is the engine with no game, so it plays
	// the game's part: a ship holding station beside the first station of the system it opens on,
	// which P flies and V looks at from behind or from its seat.
	//
	// Every system the galaxy has is defined in physics, for rails and gravity; the scene's own too.
	void DefineStarSystems();
	// The station's body, kinematic on its rails with a collision mesh from what the scene draws it
	// with (MakeCollisionMesh), and the ship beside it, both in the station's frame. Once a run: a
	// reload of the scene leaves the simulation as it was.
	void SpawnShip();
	// Whether the ship is in the system shown, which is the only one a camera can follow it in.
	bool CanPilot() const;
	bool StartPiloting(Scene::PilotView view);
	void StopPiloting();
	// The commands phase: the controls the flight context reads, as a SetFlightControls when they
	// change. A ship not flown is held where it is by its assist.
	void FlyShip();
	// The presentation phase, before the rigs: the active camera given a pilot rig on the ship's
	// proxy while it is flown, and its orbit rig back, eased, when it is not.
	void SyncPilotCamera(entt::registry &registry);
	// The ship as the traffic layer draws it, made for the ship's simulation entity alone.
	entt::entity MakeShipProxy(entt::registry &registry);
	void SetProxyFactory();
	// The ship's speed and distance from the station, and the keys, while it is flown.
	void DrawFlight();

	// Picking (docs/architecture.md, "Physics"): a left click casts a ray from the camera through
	// the cursor, as a Physics::Pick the next step answers with a Physics::Picked. Sends one through
	// the point, in window pixels, and says whether it did: not in a scene of no system.
	bool Pick(const glm::dvec2 &point);
	// The sync phase: the steps' events, a pick answered and a ship docked or undocked.
	void TakeEvents();
	// The last pick, marked where it hit and named, until the next.
	void DrawPicked();
	// A simulation entity as the shell names it.
	std::string Describe(entt::entity simEntity) const;
	// The origin of the frame the ship and the station are in at a time, in the system frame: where
	// the station is, or the primary when there is none.
	glm::dvec3 PilotFrameOrigin(double simSeconds) const;

	void RequestClose();

	// Saves (docs/architecture.md, "Persistence"): the world, the galaxy it is of, and the shell's
	// own state as a section beside it (Saves.h), as a Sim::SaveGame on the user mount under
	// saves/<name>.save. Each says whether it did, and logs why not: a save that cannot be written, or
	// one that does not load (of another galaxy, of a version this build cannot read, of a world that
	// does not fit), which leaves the world as it was.
	bool SaveTo(std::string_view name);
	bool LoadFrom(std::string_view name);
	// The bindings and the joysticks' names, written to the settings file after the player changes
	// one, so the change survives a restart.
	void SaveSettings();

	// Release every subsystem the shell made, in the reverse order of Init, from the last fence of
	// the frame loop to GLFW. Idempotent, and noexcept: it runs from the destructor, which is how
	// a run that throws out of a frame gets its teardown.
	void Shutdown() noexcept;

	// The headless sequence's side effects, bound to this shell's window and scene.
	SmokeActions MakeSmokeActions();

	AppArguments m_arguments;
	std::string m_windowTitle;
	State m_state{State::Uninitialised};

	// The user's directories: the XDG ones, or the ones under AppArguments::userDirectory. The headless
	// run with none named makes a directory of its own (m_scratchUser) and removes it in Shutdown.
	Platform::Paths m_userPaths;
	std::filesystem::path m_scratchUser;
	// The settings file in the configuration directory, read in InitUser, and the joysticks' names
	// in it, which the input feed reads its devices through.
	std::optional<Settings> m_settings;

	// The asset tree: the asset directory as the base mount, and the user's data directory over it,
	// writable, which saves go to. The scene's files and the renderer's
	// programs are found through it, so a mount added above the base can replace one.
	Vfs::FileSystem m_assets;
	// The resources read from it on the I/O worker - the scene's rigs, the renderer's models,
	// materials and textures - which hot-reload in a development build. Updated once a frame,
	// before the render. The worker outlives the manager, and the manager the scene and the
	// renderer, which hold its handles.
	std::unique_ptr<Jobs::IoWorker> m_pIoWorker;
	std::unique_ptr<Resource::ResourceManager> m_pResources;

	// The subsystems, in the order Init builds them. The renderer is the interface alone: the
	// window, the device and everything on it are behind it.
	std::unique_ptr<Scene::Scene> m_pScene;
	std::unique_ptr<Render::IRenderer> m_pRenderer;

	// Built when the frame loop starts, so its frame count and its watchdog begin with the loop.
	// Null in every interactive run, which is the shell's normal shape: an interactive run has no
	// sequence to satisfy.
	std::unique_ptr<SmokeSequence> m_smokeSequence;

	// The scene's update: made with the resource manager, which it reads the rigs from.
	std::optional<Scene::SceneUpdateSystem> m_sceneUpdateSystem;

	// The frame delta for animation. Ticked once when the loop starts, so the first frame does not
	// include the time start-up took.
	Time::RealClock m_frameClock;
	// When the frame started, read once and handed to everything the frame times.
	Time::RealClock::Clock::time_point m_frameStart{};
	// When the frame loop started: the presentation time the materials' time-driven parameters
	// read is the frame's start less this, in seconds, as a double.
	Time::RealClock::Clock::time_point m_loopStart{};
	// Whether this frame's render recreated the swapchain, for the headless sequence.
	bool m_frameResized{false};
	// The benchmark's frames so far, warm-up included, and when the timed ones began.
	std::uint32_t m_benchmarkFrame{0};
	Time::RealClock::Clock::time_point m_benchmarkStart{};
	double m_benchmarkSeconds{0.0};

	// The frame's phases: input, the simulation's steps, the sync of its snapshots into the
	// scene, animation and UI, then render (docs/architecture.md, "Frame and tick structure").
	// Each system runs on the scene's registry, which is the presentation world.
	Ecs::Scheduler m_frame;

	// The authoritative world, run as if it were a local server: the scene shows it through
	// Scene::SimProxyComponents, from the stepper's snapshots, and never holds a reference into it.
	// Empty in the shell; a game registers its components, commands and systems on it. Sixty
	// steps a second, and the seed is fixed until a game or a save chooses one.
	static constexpr double kSimStep = 1.0 / 60.0;
	static constexpr std::uint64_t kSimSeed = 0;
	Sim::SimWorld m_sim{kSimStep, kSimSeed};
	Sim::Stepper m_simStepper{m_sim};
	// Attached in the constructor, and the world's from then on.
	Physics::PhysicsWorld *m_pPhysics{nullptr};
	// The sample game's definitions, its goods and markets (Trading.h), read through the asset tree in
	// InitResources.
	std::optional<Sim::DefinitionRegistry> m_definitions;

	// The ship and the station it holds station by, as simulation entities, and the station's body
	// in its system; null until SpawnShip finds a station to put them at.
	entt::entity m_ship{entt::null};
	entt::entity m_station{entt::null};
	// The station's docking port, a sensor before its dock (Docking.h), and whether the ship is
	// docked at it, as the last event said.
	entt::entity m_port{entt::null};
	bool m_docked{false};
	std::uint64_t m_shipSystem{0};
	std::string m_stationBody;
	bool m_piloting{false};
	Scene::PilotView m_pilotView{Scene::PilotView::Chase};
	bool m_assist{true};
	// What was last sent, so a command goes only when a control moves.
	std::optional<Physics::FlightControls> m_sentControls;

	// The simulation time presentation showed last frame, which the camera a pick is drawn from
	// showed, and which the pick is cast at.
	double m_shownTime{0.0};
	// A click this frame, sent in the commands phase; the last pick's request, and whether it is
	// unanswered.
	std::optional<glm::dvec2> m_clicked;
	std::uint64_t m_pickRequest{0};
	double m_pickTime{0.0};
	bool m_pickPending{false};
	// What the last answer hit, where in the pilot frame (PilotFrameOrigin), and its normal.
	struct PickedMark
	{
		entt::entity entity{entt::null};
		double distance{0.0};
		glm::dvec3 local{0.0};
		glm::vec3 normal{0.0f};
	};
	std::optional<PickedMark> m_picked;

	// The shell's own actions, in a "shell" context at the bottom of the stack: F1 toggles the
	// dev tools, ` the console, and Escape closes the console, then the dev tools, then the
	// window. Defined in the constructor, so they exist before Init and need no window. While the
	// console is open its opaque context is on top, binding the same three, so nothing below sees
	// the keys typed into it.
	Input::InputSystem m_input;
	Input::ActionId m_toggleUi;
	Input::ActionId m_toggleConsole;
	Input::ActionId m_back;
	Input::ActionId m_toggleDebugDraw;
	// F3: the lights-per-cluster heat map in place of the shaded scene; F4: the sun's shadow cascades.
	Input::ActionId m_toggleClusterHeat;
	Input::ActionId m_toggleShadowCascades;
	// The orbit camera: W, A, S and D turn it, Q and E zoom, the right mouse button drags it, and C
	// moves it to the next body.
	Input::ActionId m_cameraOrbit;
	Input::ActionId m_cameraZoom;
	Input::ActionId m_cameraDrag;
	Input::ActionId m_cameraLook;
	Input::ActionId m_cameraNext;
	// The wheel zooms the orbit camera too, a step a notch.
	Input::ActionId m_cameraWheel;
	// J: a jump, out through the next of the shown system's lanes.
	Input::ActionId m_jumpNext;
	// P: fly the ship, and P again to let it go.
	Input::ActionId m_pilot;
	// M: the market screen, shown and hidden; L the saved games.
	Input::ActionId m_toggleMarket;
	Input::ActionId m_toggleSaves;
	// F5 saves to "quicksave", and F9 loads it.
	Input::ActionId m_quickSave;
	Input::ActionId m_quickLoad;
	// The left mouse button: pick what is under the cursor.
	Input::ActionId m_pickAction;
	GlfwInput m_glfwInput;
	bool m_consoleContext{false};
	bool m_flightContext{false};
	std::optional<PilotInput> m_pilotInput;

	// The console captures the log from construction, so start-up's messages are in it.
	DevTools::Console m_console;
	DevTools::DevToolsState m_devTools;

	// The dev tools window. On by default in the headless run, with the demo and the console,
	// because a run that renders ImGui windows exercises the UI path in the render test.
	bool m_showUI{false};

	// The frame's debug lines: cleared as the frame starts, added to by any system, drawn by the
	// renderer. F2 shows the scene's own (each mesh's axes, the light), which the headless run turns
	// on, so both line pipelines are in what the render test walks.
	Render::DebugDraw m_debugDraw;
	bool m_showSceneDebug{false};

	// The renderer's cluster check over the run, which the headless run turns on and judges once the
	// renderer is gone: copied out of it as the frame loop ends.
	Render::ClusterCheckTotals m_clusterChecks;
	// The game UI: RmlUi, drawing into the overlay the renderer draws over the scene. Made after the
	// window, so its size is the frame's, and released before the renderer.
	std::unique_ptr<Ui::UiSystem> m_pUi;
	Ui::DataModel *m_pMarketModel{nullptr};
	Ui::DocumentHandle m_marketDocument;
	bool m_showMarket{false};
	Ui::DataModel *m_pSavesModel{nullptr};
	Ui::DocumentHandle m_savesDocument;
	bool m_showSaves{false};
	SavesScreen m_savesScreen;
	// The screen's data, which the "market" model is bound to, the step it was made at, and what made
	// it: a click's answer, the lot.
	MarketScreen m_marketScreen;
	std::optional<std::uint64_t> m_marketStep;
	std::int64_t m_lot{1};
	std::string m_tradeMessage;
	// The ship's trades answered, filled or refused, and how many there were when the smoke run's
	// trade was sent.
	std::uint64_t m_tradeAnswers{0};
	std::uint64_t m_smokeTradeAnswers{0};
	bool m_marketDirty{true};
	// The warnings and errors RmlUi logged over the run, which the headless run holds at none.
	std::size_t m_uiProblems{0};
	// The frame's size the UI was last given, in pixels.
	glm::ivec2 m_uiSize{0};

	// The renderer's timings and counts as the frame loop left them, for the benchmark's report.
	std::vector<Render::PassTiming> m_passTimings;
	Render::FrameStats m_frameStats;
	std::uint64_t m_measuredFrames{0};
	// What the benchmark ran on: the device, and the frame's size in pixels.
	std::string m_deviceName;
	std::uint32_t m_frameWidth{0};
	std::uint32_t m_frameHeight{0};

	// The scene of the star system the scene names, when it names one: its bodies placed on their
	// orbits, the rest of what the system has built around the file's entities, and the traffic
	// layer. Built on each load, after the scene, and released before it.
	std::optional<Scene::SystemScene> m_systemScene;

	// The galaxy a jump goes between; empty when the assets have none, and then nothing jumps. Its
	// file's virtual path is what a save names it by.
	std::optional<Celestial::Galaxy> m_galaxy;
	std::string m_galaxyPath;
	// The galaxy's system the scene shows, if it shows one, and the jump point the last jump arrived
	// at in it.
	std::optional<std::size_t> m_shownSystem;
	std::string m_arrivedAt;
	// A jump from the system shown to another (docs/architecture.md, "Jumps"): made with the resource
	// manager, which reads the destination's files, and released before it. The simulation steps
	// through it as through any other frame; the step it began at is for its report.
	std::optional<Scene::JumpTransition> m_jump;
	std::uint64_t m_jumpStep{0};
	bool m_jumpWasActive{false};
};
} // namespace Jettison::App
