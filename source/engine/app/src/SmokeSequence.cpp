#include <jettison/app/SmokeSequence.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>

namespace Jettison::App
{
namespace
{
// A stage acts once its settling frames have passed, so the frames its action triggered are in
// the timeline before the next stage begins.
constexpr int kStageFrames = 4;

// The frames a resize is allowed to take to arrive. A window manager owns the geometry: on a
// tiling compositor (niri, which is what this is developed under, through its XWayland) an X11
// resize request is a request, and the tiler answers it with the tile it has chosen. When that is
// the size the window already has, no framebuffer resize ever arrives, and the sequence moves on
// rather than parking until the watchdog fires.
constexpr int kResizeSettleFrames = 8;

// The whole sequence on a software driver, generously: lavapipe is a few hundred milliseconds a
// frame at these sizes, and a runner is slower than this machine.
constexpr auto kWatchdog = std::chrono::seconds(120);

// The last stage has no action: it settles and closes. A run that reaches it has walked all six.
constexpr int kFinalStage = 5;

// The resize stages ask for a size relative to the current one, growing then shrinking: the
// opening size, the compositor's idea of it and the Xvfb screen all vary, and a fixed target can
// land on the size the window already has, which is not a resize at all.
constexpr int kGrowDelta = 192;
constexpr int kShrinkDelta = -128;

// The floor is what a compositor is reliably asked to give: a smaller request is one a tiler
// answers with its own tile, and the stage measures nothing.
constexpr int kMinimumWidth = 160;
constexpr int kMinimumHeight = 120;
} // namespace

SmokeSequence::SmokeSequence(SmokeActions actions, bool canIconify)
    : m_actions{std::move(actions)}, m_canIconify{canIconify}
{
}


void SmokeSequence::Advance(bool frameResized, int windowWidth, int windowHeight, TimePoint now)
{
	// A resize that has arrived ends the resize stages early: the frames that follow it are the
	// settle, and the next stage's action belongs in the frame after the settle, not in one of
	// them.
	if (frameResized && m_actionPending && m_stage <= 2)
	{
		++m_resizes;
		m_actionPending = false;
		m_frames = 0;
	}
	else if (m_actionPending && m_frames >= kResizeSettleFrames)
	{
		// The request was refused. A resize that never arrived is not one the engine handled, so
		// it is not counted: the gate counts resizes the engine recreated a swapchain for. Under
		// Xvfb with no window manager, which is where CI runs this, both requests land.
		m_actionPending = false;
		m_frames = 0;
	}

	++m_frames;

	// The stages, in order: grow the window, shrink it, minimize, restore, reload the scene,
	// settle. The last one has no action.
	if (m_stage < kFinalStage && m_frames >= kStageFrames && !m_actionPending)
	{
		m_actionPending = true;
		m_frames = 0;
		Act(m_stage, windowWidth, windowHeight);
		++m_stage;
	}

	if (m_stage == kFinalStage && m_frames >= kStageFrames)
		m_actions.close();

	// A stage that cannot finish is a hung engine, and a hung engine under CI is a hung CI. The
	// watchdog closes the window and the gate fails on the stage the run stopped at.
	if (now - m_start > kWatchdog)
		m_actions.close();
}


void SmokeSequence::Act(int stage, int windowWidth, int windowHeight)
{
	switch (stage)
	{
		case 0:
		case 1:
		{
			const int delta = stage == 0 ? kGrowDelta : kShrinkDelta;
			m_actions.resize(
			    std::max(kMinimumWidth, windowWidth + delta),
			    std::max(kMinimumHeight, windowHeight + delta * 3 / 4));
			break;
		}
		case 2:
			// Nothing reports a resize for a minimize on X11, so this stage clears its own pending
			// flag: the point is that the engine lives through the cycle, not that the cycle is
			// observed.
			if (m_canIconify)
				m_actions.iconify();
			m_actionPending = false;
			break;
		case 3:
			if (m_canIconify)
				m_actions.restore();
			m_actionPending = false;
			break;
		case 4:
			// The scene's models are destroyed and rebuilt, commonly at the addresses the pipeline
			// keyed its model resources by, between frames with a submission in flight. It is the
			// stage that has cost this engine the most.
			m_actions.reload();
			++m_reloads;
			m_actionPending = false;
			break;
		default:
			// Unreachable: Advance() only acts below the final stage.
			break;
	}
}


bool SmokeSequence::Passed() const
{
	// One handled resize, not two: the sequence runs on the compositor's terms, so a tiling window
	// manager can refuse the second request. The reload has to happen exactly once, because a
	// reload is the stage that invalidates the pipeline's model resources, and a run that never
	// reloaded has not tested that.
	return m_resizes >= 1 && m_reloads == 1 && m_stage == kFinalStage;
}


std::string SmokeSequence::Summary() const
{
	return "resizes=" + std::to_string(m_resizes) + ", reloads=" + std::to_string(m_reloads) +
	       ", stage=" + std::to_string(m_stage);
}
} // namespace Jettison::App
