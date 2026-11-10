#include <jettison/time/Clock.h>

#include <algorithm>
#include <cmath>

#include <jettison/core/Assert.h>

namespace Jettison::Time
{
RealClock::RealClock(Duration maxTick) : m_maxTick{maxTick}, m_start{Clock::now()}, m_previous{m_start}
{
	JETTISON_VERIFY(maxTick > Duration::zero());
}

RealClock::Duration RealClock::Tick()
{
	return Tick(Clock::now());
}

RealClock::Duration RealClock::Tick(Clock::time_point now)
{
	const Duration delta = now - m_previous;
	m_previous = now;
	return std::clamp(delta, Duration::zero(), m_maxTick);
}

RealClock::Duration RealClock::Elapsed() const
{
	return Clock::now() - m_start;
}

void SimClock::SetPaused(bool paused)
{
	m_paused = paused;
}

void SimClock::SetCompression(double factor)
{
	JETTISON_VERIFY(std::isfinite(factor) && factor > 0.0);
	m_compression = factor;
}

double SimClock::ScaledDelta(double realSeconds) const
{
	JETTISON_ASSERT(realSeconds >= 0.0);
	return m_paused ? 0.0 : realSeconds * m_compression;
}

void SimClock::Advance(double fixedStep)
{
	JETTISON_ASSERT(fixedStep > 0.0);
	m_now += fixedStep;
	++m_step;
}
} // namespace Jettison::Time
