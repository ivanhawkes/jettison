#include <jettison/sim/Stepper.h>

#include <jettison/sim/SimWorld.h>

#include <utility>

namespace Jettison::Sim
{
Stepper::Stepper(SimWorld &world, int maxStepsPerFrame)
    : m_world{world}, m_accumulator{world.FixedStep(), maxStepsPerFrame}
{
	Resync();
}

int Stepper::Advance(double realSeconds)
{
	const int steps = m_accumulator.Accumulate(m_world.Clock().ScaledDelta(realSeconds));
	for (int step = 0; step < steps; ++step)
	{
		// The state before the last step is what the frame interpolates from. With one step it is
		// the state the last frame ended on, which Current already holds.
		if (step == steps - 1)
			m_previous = steps == 1 ? std::move(m_current) : Capture(m_world);
		m_world.Step();
	}
	if (steps > 0)
		m_current = Capture(m_world);
	return steps;
}

void Stepper::Resync()
{
	m_current = Capture(m_world);
	m_previous = m_current;
}
} // namespace Jettison::Sim
