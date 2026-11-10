#pragma once

namespace Jettison::Time
{
// Turns real frame time into a number of fixed simulation steps: accumulate the scaled delta,
// hand out whole steps, keep the remainder for interpolation. The accumulator is clamped so a
// slow frame cannot demand so many steps that the next frame is slower still (the spiral of
// death). Filled in phase 2.
class FixedStepAccumulator
{
  public:
	explicit FixedStepAccumulator(double fixedStep, int maxStepsPerFrame = 8);

	// Adds a frame's worth of simulation time and returns how many fixed steps to run.
	int Accumulate(double scaledDelta);
	// The fraction of a step not yet simulated, in [0, 1), for interpolating presentation.
	double Alpha() const;
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
	[[maybe_unused]] double m_accumulated{0.0};
};
} // namespace Jettison::Time
