#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jettison/app/AppArguments.h>
#include <jettison/app/Game.h>
#include <jettison/app/SmokeSequence.h>
#include <jettison/audio/AudioSystem.h>
#include <jettison/audio/EventSounds.h>
#include <jettison/audio/SoundSources.h>
#include <jettison/devtools/Console.h>
#include <jettison/devtools/Theme.h>
#include <jettison/devtools/Views.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/input/Actions.h>
#include <jettison/platform/Paths.h>
#include <jettison/render/Clusters.h>
#include <jettison/render/DebugDraw.h>
#include <jettison/render/FrameStats.h>
#include <jettison/scene/SceneSystems.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Reason.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Stepper.h>
#include <jettison/time/Clock.h>
#include <jettison/ui/UiSystem.h>
#include <jettison/vfs/FileSystem.h>

#include "GlfwInput.h"
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
// so that destructor is the whole safety net.
//
// The state machine is private and exists for one reason: Shutdown() is guarded by it, so the
// loop and ~Application can both ask for teardown and the second one is free.
//
// A game composed into it (Game) is given its points in the lifecycle, and reads the shell through
// Shell; with none, the shell is the engine with no game, and its hooks are Game's own, which do
// nothing.
class Application final : private Shell
{
	// The app suite's view of the shell's stages, which it runs without a window (GameTests.cpp).
	friend struct ApplicationProbe;

  public:
	Application(AppArguments arguments, std::string windowTitle, Game *game = nullptr);

	~Application();

	Application(const Application &) = delete;

	Application &operator=(const Application &) = delete;

	// Init, the frame loop, Shutdown, then the headless sequence's verdict. A sequence that did
	// not walk its stages throws: it is the render test's gate, and a run that got there by
	// skipping a stage has to fail. Teardown has run by the time this throws, and ~Application
	// covers the paths that throw earlier.
	void Run();

  private:
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

	// Loads the scene, then the game's hook (Game::SceneLoaded).
	void LoadScene();

	// The smoke sequence's reload: the game told the scene is going (Game::SceneUnloading), and it
	// loaded again.
	void ReloadScene();

	void InitRenderer();

	void InitWindow();

	// The game UI over the window (docs/ui.md): its fonts, the saved games' data model
	// and document, read through the asset tree, then the game's (Game::OpenUi).
	void InitUi();

	// The game's sound (docs/audio.md): the mixer, its buses' volumes from the
	// settings, the event sounds the definitions name, and the music. Needs no window.
	void InitAudio();

	// The listener at the camera, the clips' events, the scene's sources and the mixer's frame.
	void UpdateAudio(entt::registry &registry);

	// The bus's volume from the settings, or 1 when they name none.
	float SettingVolume(Audio::Bus bus);

	// Plays what the data maps the event to, when the game says the player hears it (Game::Hears).
	void PlayEventSound(const Sim::SimEvent &event);

	// Sounds started on the UI bus, which the smoke run's trade has to add to.
	std::uint64_t UiSounds() const override;

	// The frame's systems, one or two to a phase; registered in the constructor, so a mistake in
	// the order is a start-up error, before a window opens.
	void RegisterFrameSystems();

	// The frame loop, and one frame of it: the scheduler's phases in order (Ecs::kTickPhases).
	void RunFrameLoop();

	void Tick();

	// Runs a --run line as typed into the console; a line the console refuses throws, since a frame
	// captured from the wrong place is worse than none.
	void RunCommandLine(const std::string &line);

	void PollInput();

	// The frame's camera input, into the scene's orbit rigs.
	void DriveCameras(entt::registry &registry, double deltaTime);

	// Moves each orbit camera to the next target (OrbitTarget), with a transition.
	void NextCameraTarget(entt::registry &registry);

	// Points the orbit camera at the target, out to three of its radii, eased from wherever the camera
	// is.
	void FrameTarget(entt::registry &registry, entt::entity camera, entt::entity target);

	// The console's orbit command: every orbit camera placed at once at a yaw, pitch and distance,
	// about the target named if one is. Throws, saying why, on what it cannot do.
	void PlaceOrbitCamera(std::span<const std::string> arguments);

	// The console's grade: the scene's colour grade set, replaced or taken away.
	void SetGrade(std::span<const std::string> arguments);

	// The console's language: the screens read again in the language given, kept in the settings, or
	// with none, the language they are in and those there are tables for.
	void SetLanguage(DevTools::Console &console, std::span<const std::string> arguments);

	void DrawUI();

	// The presentation phase: the game's screens made again (Game::UpdateUi), and the game UI laid
	// out and drawn into its overlay.
	void UpdateGameUi() override;

	// The window's events since the last frame, to the game UI, unless the dev tools have the mouse or
	// the keyboard.
	void FeedGameUi();

	// The saved-games screen, its list read again from the user mount as it is shown.
	void ShowSaves(bool shown);

	void DrawRendererPanel();

	void DrawScenePanel();

	void AdvanceBenchmark();

	void ReportBenchmark() const;

	// Pushes and pops the game's and the console's input contexts to match the game's
	// (SetGameContext) and whether the console is open: the console's over the game's, over the
	// shell's.
	void SyncContexts();

	void PlaySequence(std::string_view name, double from) override;

	// The sequence playing, cut short: the camera is its rigs' at once.
	void StopSequence();

	// What the playing sequence shows over the frame: its fade and its bars.
	void ShowSequence(entt::registry &registry);

	// Picking (docs/physics.md): a left click casts a ray from the camera through
	// the cursor, as a Physics::Pick the next step answers with a Physics::Picked, where the game says
	// (Game::PickIn). Sends one through the point, in window pixels, and says whether it did: not
	// where the game has no pick space.
	bool Pick(const glm::dvec2 &point);

	void ClearPick() override;

	// The sync phase: the steps' events, to the scripts, the sounds, the pick and the game.
	void TakeEvents();

	void DrawPicked();

	std::string Describe(entt::entity simEntity) const override;

	// A reason the step gave, as the player reads it, from the newest snapshot (Reasons.h).
	std::string Word(const Sim::Reason &reason) const override;

	void RequestClose();

	// Saves (docs/persistence.md): the world, the scripts' store and the game's
	// sections and details (Game::WriteSave), as a Sim::SaveGame on the user mount under
	// saves/<name>.save. Each says whether it did, and logs why not: a save that cannot be written, or
	// one that does not load (one the game refuses, of a version this build cannot read, of a world
	// that does not fit), which leaves the world as it was.
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

	// The shell as the game reads it (Shell).
	const AppArguments &Arguments() const override;

	Sim::SimWorld &World() override;

	void Resync() override;

	Scene::Scene &Scene() override;

	const Sim::Snapshot &Snapshot() const override;

	const Sim::Snapshot &Previous() const override;

	double Alpha() const override;

	entt::entity ActiveCamera() const override;

	Render::DebugDraw &DebugLines() override;

	bool DebugShown() const override;

	void SetFade(const glm::vec3 &colour, float amount) override;

	Vfs::FileSystem &Assets() override;

	Resource::ResourceManager &Resources() override;

	DevTools::Console &Console() override;

	Input::InputSystem &Input() override;

	void SetGameContext(std::string_view context) override;

	const Sim::DefinitionRegistry &Definitions() const override;

	Script::ScriptHost &Scripts() override;

	Sim::Controller Player() const override;

	void Frame(entt::entity target) override;

	Ui::UiSystem *GameUi() override;

	bool SmokeTest() const override;

	// The world's physics as a game's hooks are given it.
	ShellWorld MakeShellWorld();

	AppArguments m_arguments;
	std::string m_windowTitle;
	// The game composed into the shell, or the shell's own Game, which does nothing.
	Game m_noGame;
	Game *m_pGame;
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
	// The mods mounted, in order, with their versions: what a save records it was made with.
	std::vector<Sim::ModVersion> m_mods;
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
	// The clips' events the scene's update crossed this frame, for the audio's update to play.
	std::vector<Scene::ClipEventFired> m_clipEvents;
	// The cinematics' player, after the camera rigs; made with the resource manager it reads them
	// from.
	std::optional<Scene::SequenceSystem> m_sequences;
	// Whether the last frame showed a sequence's fade, which the next clears once it is over.
	bool m_sequenceFaded{false};

	// The frame delta for animation. Ticked once when the loop starts, so the first frame does not
	// include the time start-up took.
	Time::RealClock m_frameClock;
	// When the frame started, read once and handed to everything the frame times.
	Time::RealClock::Clock::time_point m_frameStart{};
	// When the frame loop started: the presentation time the materials' time-driven parameters
	// read is the frame's start less this, in seconds, as a double.
	Time::RealClock::Clock::time_point m_loopStart{};
	// The clocks' seconds since the loop started, when each frame moves them a fixed step
	// (AppArguments::frameStep).
	double m_fixedTime{0.0};
	// Frames ticked, held ones included: against m_fixedTime, how many a load held.
	std::uint64_t m_frameIndex{0};
	// Frames in a row begun with nothing loading, and whether the screenshot the run was asked for
	// has been taken.
	std::uint32_t m_quietFrames{0};
	bool m_screenshotTaken{false};
	// The --run lines given a frame or a simulation time that have not run yet, in the order given.
	std::vector<RunCommand> m_pendingCommands;
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
	// Physics alone in the shell; a game registers its components, commands and systems on it.
	// Sixty steps a second, and the seed is fixed until a game or a save chooses one.
	static constexpr double kSimStep = 1.0 / 60.0;
	static constexpr std::uint64_t kSimSeed = 0;
	Sim::SimWorld m_sim{kSimStep, kSimSeed};
	Sim::Stepper m_simStepper{m_sim};
	// Attached in the constructor, and the world's from then on.
	Physics::PhysicsWorld *m_pPhysics{nullptr};
	// The definitions, the engine's and the game's (Game::LoadDefinitions), read through the asset tree
	// in InitResources.
	std::optional<Sim::DefinitionRegistry> m_definitions;
	// The mods' scripts (defs/scripts), made beside the definitions in InitResources: given every
	// event TakeEvents takes, and answering the traders' inputs they name. After the world, so it goes
	// before it, and no step runs once it has.
	std::unique_ptr<Script::ScriptHost> m_pScripts;

	// The simulation time presentation showed last frame, which the camera a pick is drawn from
	// showed, and which the pick is cast at.
	double m_shownTime{0.0};
	// A click this frame, sent in the commands phase; the last pick's request, and whether it is
	// unanswered.
	std::optional<glm::dvec2> m_clicked;
	std::uint64_t m_pickRequest{0};
	double m_pickTime{0.0};
	bool m_pickPending{false};
	// What the last answer hit, where relative to the origin of its frame (Game::PickOrigin), and its
	// normal.
	struct PickedMark
	{
		entt::entity entity{entt::null};
		double distance{0.0};
		glm::dvec3 local{0.0};
		glm::vec3 normal{0.0f};
	};
	std::optional<PickedMark> m_picked;
	// The outline on what was picked, placed on its proxy each frame (MarkScene), and the proxy the
	// shell placed it on, to take it off again.
	entt::entity m_outlined{entt::null};

	void MarkScene(entt::registry &registry);

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
	Input::ActionId m_toggleAmbientAccess;
	// The orbit camera: W, A, S and D turn it, Q and E zoom, the right mouse button drags it, and C
	// moves it to the next target.
	Input::ActionId m_cameraOrbit;
	Input::ActionId m_cameraZoom;
	Input::ActionId m_cameraDrag;
	Input::ActionId m_cameraLook;
	Input::ActionId m_cameraNext;
	// The wheel zooms the orbit camera too, a step a notch.
	Input::ActionId m_cameraWheel;
	// L: the saved games, shown and hidden.
	Input::ActionId m_toggleSaves;
	// F5 saves to "quicksave", and F9 loads it.
	Input::ActionId m_quickSave;
	Input::ActionId m_quickLoad;
	// The left mouse button: pick what is under the cursor.
	Input::ActionId m_pickAction;
	GlfwInput m_glfwInput;
	bool m_consoleContext{false};
	// The game's context, and the one pushed.
	std::string m_gameContext;
	std::string m_pushedGameContext;

	// The dev tools' theme, declared before the console that binds it as `dev.*` variables and holds
	// references to it: members go in reverse order, so the console is destroyed first and its
	// references never outlast what they refer to.
	DevTools::ThemeSettings m_theme;
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
	// The game's sound, which the events of the player's own vehicle play through. Released before the
	// UI; it holds nothing of the renderer's.
	std::unique_ptr<Audio::AudioSystem> m_pAudio;
	std::optional<Audio::EventSounds> m_eventSounds;
	Audio::SoundSources m_soundSources;
	Audio::SourceHandle m_music;
	// The warnings and errors the audio logged over the run, and the sounds that would not play,
	// which the headless run holds at none.
	std::size_t m_audioProblems{0};
	Ui::DataModel *m_pSavesModel{nullptr};
	Ui::DocumentHandle m_savesDocument;
	bool m_showSaves{false};
	SavesScreen m_savesScreen;
	// The warnings and errors RmlUi logged over the run, which the headless run holds at none.
	std::size_t m_uiProblems{0};
	// The frame's size the UI was last given, in pixels.
	glm::ivec2 m_uiSize{0};

	// The renderer's timings and counts as the frame loop left them, for the benchmark's report.
	std::vector<Render::PassTiming> m_passTimings;
	bool m_benchmarkAmbientOcclusion{false};
	Render::FrameStats m_frameStats;
	std::uint64_t m_measuredFrames{0};
	// What the benchmark ran on: the device, and the frame's size in pixels.
	std::string m_deviceName;
	std::uint32_t m_frameWidth{0};
	std::uint32_t m_frameHeight{0};
};
} // namespace Jettison::App
