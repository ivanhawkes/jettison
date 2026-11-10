#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <jettison/app/AppArguments.h>
#include <jettison/app/SmokeSequence.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/devtools/Console.h>
#include <jettison/devtools/Views.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/input/Actions.h>
#include <jettison/render/DebugDraw.h>
#include <jettison/render/SceneSystems.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Stepper.h>
#include <jettison/time/Clock.h>
#include <jettison/vfs/FileSystem.h>

#include "GlfwInput.h"

namespace Jettison::Render
{
class DeviceContext;
class ImGuiRenderer;
class Renderer;
class Scene;
class Swapchain;
class Window;
} // namespace Jettison::Render

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

	// The stages of Init(), in dependency order: the window carries the surface, the device
	// context carries the device the rest of them allocate on.
	void Init();
	void InitWindow();
	void InitVulkan();
	void InitScene();
	// Loads the scene, and the star system it names; the smoke sequence's reload comes here too.
	void LoadScene();
	void InitRenderer();
	void InitUI();

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
	void DrawUI();
	// Pushes or pops the console's input context to match whether the console is open.
	void SyncConsoleContext();

	void RequestClose();

	// Release every subsystem the shell made, in the reverse order of Init, from the last fence of
	// the frame loop to GLFW. Idempotent, and noexcept: it runs from the destructor, which is how
	// a run that throws out of a frame gets its teardown.
	void Shutdown() noexcept;

	// The headless sequence's side effects, bound to this shell's window and scene.
	SmokeActions MakeSmokeActions();

	AppArguments m_arguments;
	std::string m_windowTitle;
	State m_state{State::Uninitialised};

	// The asset tree: the asset directory as the base mount. The renderer finds its shader programs
	// through it, so a mount added above the base can replace one.
	Vfs::FileSystem m_assets;

	// The subsystems, in the order Init builds them. Shared pointers where a subsystem is handed
	// to more than one of its neighbours - the device context to all of them, the window to the
	// device context and the renderer - and the shell holds the last reference to the rest.
	std::shared_ptr<Render::Window> m_pWindow;
	std::shared_ptr<Render::DeviceContext> m_pDeviceContext;
	std::shared_ptr<Render::Swapchain> m_pSwapchain;
	std::shared_ptr<Render::Renderer> m_pRenderer;
	std::unique_ptr<Render::ImGuiRenderer> m_pImGuiRenderer;
	std::unique_ptr<Render::Scene> m_scene;

	// Built when the frame loop starts, so its frame count and its watchdog begin with the loop.
	// Null in every interactive run, which is the shell's normal shape: an interactive run has no
	// sequence to satisfy.
	std::unique_ptr<SmokeSequence> m_smokeSequence;

	// The scene systems carry no state; handing them to the renderer keeps the scene graph's
	// update and collect policies the shell's choice rather than the renderer's.
	const Render::SceneUpdateSystem m_sceneUpdateSystem;
	const Render::SceneRenderSystem m_sceneRenderSystem;

	// The frame delta for animation. Ticked once when the loop starts, so the first frame does not
	// include the time start-up took.
	Time::RealClock m_frameClock;
	// When the frame started, read once and handed to everything the frame times.
	Time::RealClock::Clock::time_point m_frameStart{};
	// Whether this frame's render recreated the swapchain, for the headless sequence.
	bool m_frameResized{false};

	// The frame's phases: input, the simulation's steps, the sync of its snapshots into the
	// scene, animation and UI, then render (docs/architecture.md, "Frame and tick structure").
	// Each system runs on the scene's registry, which is the presentation world.
	Ecs::Scheduler m_frame;

	// The authoritative world, run as if it were a local server: the scene shows it through
	// SimProxyComponents, from the stepper's snapshots, and never holds a reference into it.
	// Empty in the shell; a game registers its components, commands and systems on it. Sixty
	// steps a second, and the seed is fixed until a game or a save chooses one.
	static constexpr double kSimStep = 1.0 / 60.0;
	static constexpr std::uint64_t kSimSeed = 0;
	Sim::SimWorld m_sim{kSimStep, kSimSeed};
	Sim::Stepper m_simStepper{m_sim};

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
	// The orbit camera: W, A, S and D turn it, Q and E zoom, the right mouse button drags it, and C
	// moves it to the next body.
	Input::ActionId m_cameraOrbit;
	Input::ActionId m_cameraZoom;
	Input::ActionId m_cameraDrag;
	Input::ActionId m_cameraLook;
	Input::ActionId m_cameraNext;
	GlfwInput m_glfwInput;
	bool m_consoleContext{false};

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

	// The star system the scene's celestial bodies come from, when it names one.
	std::optional<Celestial::StarSystem> m_starSystem;
};
} // namespace Jettison::App
