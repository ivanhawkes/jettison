#pragma once

namespace Jettison::Time
{
// Turns real frame time into a number of fixed simulation steps: accumulate the scaled delta,
// hand out whole steps, keep the remainder for interpolation. The steps handed out per frame are
// capped, so a slow frame cannot demand so many steps that the next frame is slower still (the
// spiral of death). Whole steps over the cap are dropped, and counted; the fraction of a step is
// kept, so interpolation stays continuous across a dropped frame.
class FixedStepAccumulator
{
  public:
	// fixedStep is positive, in simulation seconds; maxStepsPerFrame is at least 1.
	explicit FixedStepAccumulator(double fixedStep, int maxStepsPerFrame = 8);

	// Adds a frame's worth of simulation time (SimClock::ScaledDelta) and returns how many fixed
	// steps to run.
	int Accumulate(double scaledDelta);
	// The fraction of a step not yet simulated, in [0, 1), for interpolating presentation.
	double Alpha() const;
	// Simulation seconds the cap has dropped since construction, for the console and the
	// profiler: a number that grows says the simulation cannot keep up with the requested rate.
	double DroppedTime() const
	{
		return m_droppedTime;
	}
	double FixedStep() const
	{
		return m_fixedStep;
	}
	int MaxStepsPerFrame() const
	{
		return m_maxStepsPerFrame;
	}

  private:
	double m_fixedStep;
	int m_maxStepsPerFrame;
	double m_accumulated{0.0};
	double m_droppedTime{0.0};
};
} // namespace Jettison::Time
