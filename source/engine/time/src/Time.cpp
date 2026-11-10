#include <jettison/time/Calendar.h>
#include <jettison/time/Clock.h>
#include <jettison/time/FixedStep.h>

#include <jettison/core/Assert.h>

namespace Jettison::Time
{
RealClock::Duration RealClock::Tick()
{
	JETTISON_NOT_IMPLEMENTED();
}
RealClock::Duration RealClock::Elapsed() const
{
	JETTISON_NOT_IMPLEMENTED();
}

void SimClock::SetPaused(bool /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void SimClock::SetCompression(double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void SimClock::Advance(double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}

FixedStepAccumulator::FixedStepAccumulator(double fixedStep, int maxStepsPerFrame)
    : m_fixedStep{fixedStep}, m_maxStepsPerFrame{maxStepsPerFrame}
{
}
int FixedStepAccumulator::Accumulate(double /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
double FixedStepAccumulator::Alpha() const
{
	JETTISON_NOT_IMPLEMENTED();
}

Calendar::Calendar(double secondsPerDay, std::int32_t daysPerYear)
    : m_secondsPerDay{secondsPerDay}, m_daysPerYear{daysPerYear}
{
}
Date Calendar::FromSimSeconds(double /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
double Calendar::ToSimSeconds(const Date & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
std::string Calendar::Format(const Date & /*unused*/) const
{
	JETTISON_NOT_IMPLEMENTED();
}
} // namespace Jettison::Time
