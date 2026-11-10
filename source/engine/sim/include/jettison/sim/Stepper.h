#pragma once

#include <jettison/sim/Snapshot.h>
#include <jettison/time/FixedStep.h>

namespace Jettison::Sim
{
class SimWorld;

// Runs a world at the display's pace, as the frame's simulation phase (docs/architecture.md,
// "Frame and tick structure"): a frame's real seconds, scaled by the world's clock, become zero or
// more fixed steps through the accumulator, and the snapshots either side of the last step are
// kept for the sync phase to interpolate between, Alpha() of the way. Presentation therefore
// shows the world one step behind, which is what lets it move smoothly at any display rate.
//
// Only the last two states are interpolated between, so a frame of many steps, under time
// compression, captures twice rather than once a step.
class Stepper
{
  public:
	// Captures the world as both snapshots. The world outlives the stepper.
	explicit Stepper(SimWorld &world, int maxStepsPerFrame = 8);

	// Runs the steps the frame's real seconds make, and returns how many. An exception from a
	// step propagates with the world part-way through it (SimWorld::Step), and the snapshots are
	// then whatever they were left as until a Resync.
	int Advance(double realSeconds);

	// Captures the world as both snapshots again: after a load, or anything else that changes the
	// world outside a step, so presentation does not interpolate across the change.
	void Resync();

	const Snapshot &Previous() const
	{
		return m_previous;
	}
	const Snapshot &Current() const
	{
		return m_current;
	}
	// How far from Previous to Current presentation is, in [0, 1).
	double Alpha() const
	{
		return m_accumulator.Alpha();
	}
	const Time::FixedStepAccumulator &Accumulator() const
	{
		return m_accumulator;
	}

  private:
	SimWorld &m_world;
	Time::FixedStepAccumulator m_accumulator;
	Snapshot m_previous;
	Snapshot m_current;
};
} // namespace Jettison::Sim
