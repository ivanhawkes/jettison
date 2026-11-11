#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace Jettison::App
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
	// Flies the system's ship, behind it or from its seat, and says whether there was one to fly:
	// false in a scene of no system, or of one with no station.
	std::function<bool(bool cockpit)> pilot;
	// Picks through the middle of the window, and says whether a pick was sent: false in a scene of
	// no system. Then whether it is still unanswered.
	std::function<bool()> pick;
	std::function<bool()> picking;
	// Saves the game to the user mount and loads it back, and says whether the world loaded is the
	// world saved, by its state hash.
	std::function<bool()> saveAndLoad;
	// Starts a jump to another star system, and says whether one started: false where there is no
	// galaxy to jump in. Then whether it is still under way.
	std::function<bool()> jump;
	std::function<bool()> jumping;
	std::function<void()> close;
};

// The headless render test's sequence, in eleven stages of a handful of frames each: two resizes (a
// swapchain recreate), a minimize and restore (a surface that reports no extent), a scene reload
// (models destroyed and rebuilt, commonly at the addresses the pipeline keyed its resources by), a
// ship flown behind the chase camera and then from the cockpit (the ship's proxy made by the
// traffic layer, the station's collision mesh, the flight commands, and the pilot rigs), a pick
// through the middle of the window (a ray from the camera cast by physics and answered as an
// event), a save and a load of it (the world, physics' state among it, written durably and read
// back, with the scene's proxies on it), a jump to another star system (the scene replaced behind
// its effect, with frames in flight, and the new system streamed in), and a settle. Each stage acts,
// waits for the frames it triggered to settle, then advances; validation and the sanitizers read the
// result, so the sequence only has to reach the end.
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

	// The counters in the words the failure report uses: "resizes=1, reloads=1, pilots=2, picks=1,
	// saves=1, jumps=1, stage=10".
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
	// The piloting stages that had a ship to fly. Not a verdict: the sample scene has no station.
	int m_pilots{0};
	// The picks answered. Not a verdict either: a pick needs a system's physics to be cast in.
	int m_picks{0};
	bool m_pickPending{false};
	// Saves that loaded back to the state saved.
	int m_saves{0};
	int m_jumps{0};
	// The jump stage's action is pending until the jump is over, however many frames that takes.
	bool m_jumpPending{false};
};
} // namespace Jettison::App
