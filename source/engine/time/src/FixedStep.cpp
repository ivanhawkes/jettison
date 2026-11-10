#include <jettison/time/FixedStep.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include <jettison/core/Assert.h>

namespace Jettison::Time
{
namespace
{
// The largest double below 1.
constexpr double kBelowOne = 1.0 - std::numeric_limits<double>::epsilon() / 2.0;
} // namespace

FixedStepAccumulator::FixedStepAccumulator(double fixedStep, int maxStepsPerFrame)
    : m_fixedStep{fixedStep}, m_maxStepsPerFrame{maxStepsPerFrame}
{
	JETTISON_VERIFY(std::isfinite(fixedStep) && fixedStep > 0.0);
	JETTISON_VERIFY(maxStepsPerFrame >= 1);
}

int FixedStepAccumulator::Accumulate(double scaledDelta)
{
	JETTISON_ASSERT(scaledDelta >= 0.0);
	m_accumulated += scaledDelta;
	// The quotient can round to the next whole number when the remainder is a hair short of a
	// step, which leaves the remainder a hair below zero; it is clamped rather than carried as
	// debt into the next frame.
	const double whole = std::floor(m_accumulated / m_fixedStep);
	m_accumulated = std::max(0.0, m_accumulated - whole * m_fixedStep);
	// Compared as doubles before the conversion: a debugger pause can make whole too large for an
	// int.
	if (whole <= static_cast<double>(m_maxStepsPerFrame))
		return static_cast<int>(whole);
	m_droppedTime += (whole - static_cast<double>(m_maxStepsPerFrame)) * m_fixedStep;
	return m_maxStepsPerFrame;
}

double FixedStepAccumulator::Alpha() const
{
	// The opposite rounding can leave the remainder a hair at or above one step; it is a step the
	// next frame hands out, and until then the interpolation stays below 1.
	return std::min(m_accumulated / m_fixedStep, kBelowOne);
}
} // namespace Jettison::Time
