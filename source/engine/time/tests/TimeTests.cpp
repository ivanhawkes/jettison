#include <jettison/time/Calendar.h>
#include <jettison/time/Clock.h>
#include <jettison/time/FixedStep.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

using Jettison::Core::NotImplementedError;
using namespace Jettison::Time;

TEST_CASE("Time: a new simulation clock is at zero, running, in real time")
{
	const SimClock clock;
	CHECK(clock.Now() == 0.0);
	CHECK(clock.Step() == 0);
	CHECK_FALSE(clock.Paused());
	CHECK(clock.Compression() == 1.0);
}

TEST_CASE("Time: the stubs fail loudly until phase 2")
{
	SimClock clock;
	CHECK_THROWS_AS(clock.Advance(1.0 / 60.0), NotImplementedError);
	FixedStepAccumulator accumulator{1.0 / 60.0};
	CHECK(accumulator.FixedStep() == doctest::Approx(1.0 / 60.0));
	CHECK_THROWS_AS(accumulator.Accumulate(0.1), NotImplementedError);
	const Calendar calendar{86400.0, 365};
	CHECK_THROWS_AS(calendar.FromSimSeconds(0.0), NotImplementedError);
}
