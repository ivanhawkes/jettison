#pragma once

#include <chrono>
#include <functional>
#include <optional>
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
	// Pilots what the game gives to fly, behind it or from its seat, and says whether there was
	// anything to fly: false in a scene with nothing to pilot. A game may send traffic alongside,
	// flown by steering.
	std::function<bool(bool cockpit)> pilot;
	// Then whether what was flown has still to show it moved as the stage asked, which the stage waits
	// for, as long as it takes, since it moves by simulation time and a stage's frames are fast or
	// slow with the machine. Left empty by a game whose stage is over once it starts.
	std::function<bool()> piloting;
	// Picks through the middle of the window, and says whether a pick was sent: false in a scene of
	// no physics frame. Then whether it is still unanswered.
	std::function<bool()> pick;
	std::function<bool()> picking;
	// Saves the game to the user mount and loads it back, and says whether the world loaded is the
	// world saved, by its state hash.
	std::function<bool()> saveAndLoad;
	// The game's: shows a screen of its, checks it shows what the snapshot has, and presses a control
	// that sends a command: none where the game has no screen to drive here, false where the screen
	// does not show what the snapshot has. Then whether the command's answer is still to come, and
	// once it has come, whether the screen shows it.
	std::function<std::optional<bool>()> gameUi;
	std::function<bool()> gameUiWaiting;
	std::function<bool()> gameUiAnswered;
	std::function<void()> close;
};

// The headless render test's sequence, in eleven stages of a handful of frames each: two resizes (a
// swapchain recreate), a minimize and restore (a surface that reports no extent), a scene reload
// (models destroyed and rebuilt, commonly at the addresses the pipeline keyed its resources by),
// what the game pilots flown behind it and then from its seat (its proxy made from the snapshots,
// the collision meshes it meets, the flight commands, and the pilot rigs), with whatever traffic
// the game steers (the spatial index and steering, and the traffic's proxies), a pick through the
// middle of the window (a ray from the camera cast by physics and answered as an event), a save and
// a load of it (the world, physics' state among it, written durably and read back, with the scene's
// proxies on it), the game's screen (a document bound to the snapshot, its control sending a
// command, the step's answer shown), and a settle. Each stage acts, waits for the frames it triggered
// to settle, then advances; validation and the sanitizers read the result, so the sequence only has to
// reach the end.
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
	// saves=1, screens=1, answers=1, stage=10".
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
	// The piloting stages that had something to fly. Not a verdict: the shell has nothing to pilot.
	int m_pilots{0};
	// The picks answered. Not a verdict either: a pick needs a physics frame to be cast in.
	int m_picks{0};
	bool m_pickPending{false};
	// A piloting stage's action is pending until the game says what it flew has shown it moved.
	bool m_pilotPending{false};
	// Saves that loaded back to the state saved.
	int m_saves{0};
	// The game's screens that showed the snapshot and sent their command, and the answers among them
	// the screen then showed: a verdict, every screen tried has to be answered.
	int m_screens{0};
	int m_answers{0};
	bool m_gameUiPending{false};
	bool m_screenWrong{false};
};
} // namespace Jettison::App
