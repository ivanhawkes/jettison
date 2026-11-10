#pragma once

#include <chrono>
#include <memory>
#include <string>

#include <jettison/app/AppArguments.h>
#include <jettison/app/SmokeSequence.h>
#include <jettison/render/SceneSystems.h>

namespace Jettison::Render
{
class DeviceContext;
class ImGuiRenderer;
class Pipeline;
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
	void InitPipeline();
	void InitUI();
	void InitRenderer();

	// The frame loop, and one frame of it: events, hot keys, UI, render, sequence.
	void RunFrameLoop();
	void Tick();
	void PollInput();

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

	// The subsystems, in the order Init builds them. Shared pointers where a subsystem is handed
	// to more than one of its neighbours - the device context to all of them, the window to the
	// device context and the renderer - and the shell holds the last reference to the rest.
	std::shared_ptr<Render::Window> m_pWindow;
	std::shared_ptr<Render::DeviceContext> m_pDeviceContext;
	std::shared_ptr<Render::Swapchain> m_pSwapchain;
	std::shared_ptr<Render::Pipeline> m_pPipeline;
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

	using Clock = std::chrono::steady_clock;

	// The frame start the previous frame's is subtracted from, for the animation delta.
	Clock::time_point m_previousFrame{Clock::now()};

	// The demo window, toggled by F1, dismissed by Escape. On by default in the headless run,
	// because a run that renders an ImGui window exercises the UI path in the render test.
	bool m_showUI{false};
	bool m_f1WasDown{false};
	bool m_escapeWasDown{false};
};
} // namespace Jettison::App
