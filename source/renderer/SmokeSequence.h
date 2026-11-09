#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace Jettison::Renderer
{
// The side effects the smoke sequence drives the engine into, performed by the app shell, which
// owns the window and the scene. Callbacks rather than calls: the sequence stays a state machine
// that knows nothing about GLFW, Vulkan or the scene graph, so reading it gives you the list of
// things that have hurt this engine and nothing else.
struct SmokeActions
{
	std::function<void(int width, int height)> resize;
	std::function<void()> iconify;
	std::function<void()> restore;
	std::function<void()> reload;
	std::function<void()> close;
};

// The headless render test's sequence, in six stages of a handful of frames each: two resizes (a
// swapchain recreate), a minimize and restore (a surface that reports no extent), a scene reload
// (models destroyed and rebuilt, commonly at the addresses the pipeline keyed its resources by)
// and a settle. Each stage acts, waits for the frames it triggered to settle, then advances;
// validation and the sanitizers read the result, so the sequence only has to reach the end.
//
// One frame at a time, from the shell's loop: the stages are defined by frames, not by wall
// clock, because the point of each is the frame that the action caused.
class SmokeSequence
{
  public:
	using Clock = std::chrono::steady_clock;
	using TimePoint = Clock::time_point;

	// canIconify is false where minimizing cannot be arranged from inside the frame loop, which
	// is every Wayland session: the stages still run, without the window manager call.
	explicit SmokeSequence(SmokeActions actions, bool canIconify);

	SmokeSequence(const SmokeSequence &) = delete;
	SmokeSequence &operator=(const SmokeSequence &) = delete;

	// One frame. frameResized says this frame recreated the swapchain, which is the only way the
	// sequence can see a resize the engine handled; windowWidth and windowHeight are the window's
	// current size, which the resize stages move relative to.
	void Advance(bool frameResized, int windowWidth, int windowHeight, TimePoint now);

	// The gate. Reaching the last stage is not enough: a stage that ran with its action refused
	// by a window manager is a stage the engine was not tested on.
	bool Passed() const;

	// The counters in the words the failure report uses: "resizes=1, reloads=1, stage=5".
	std::string Summary() const;

  private:
	// The action of one stage, taken before the stage advances.
	void Act(int stage, int windowWidth, int windowHeight);

	SmokeActions m_actions;
	bool m_canIconify{false};
	TimePoint m_start{Clock::now()};

	// Frames since the stage began, and since an action is owed its settling frames.
	int m_frames{0};
	bool m_actionPending{false};

	int m_stage{0};
	int m_resizes{0};
	int m_reloads{0};
};
} // namespace Jettison::Renderer
