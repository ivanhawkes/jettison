#include <jettison/time/Clock.h>
#include <jettison/time/FixedStep.h>

#include <doctest/doctest.h>

using namespace Jettison::Time;

namespace
{
constexpr double kStep = 1.0 / 60.0;
} // namespace

TEST_CASE("FixedStep: a frame one step long runs exactly one step, every frame")
{
	FixedStepAccumulator accumulator{kStep};
	for (int frame = 0; frame < 600; ++frame)
	{
		CHECK(accumulator.Accumulate(kStep) == 1);
		CHECK(accumulator.Alpha() == 0.0);
	}
	CHECK(accumulator.DroppedTime() == 0.0);
}

TEST_CASE("FixedStep: frames shorter than a step carry their remainder for interpolation")
{
	FixedStepAccumulator accumulator{kStep};
	CHECK(accumulator.Accumulate(kStep / 2.0) == 0);
	CHECK(accumulator.Alpha() == doctest::Approx(0.5));
	CHECK(accumulator.Accumulate(kStep / 2.0) == 1);
	CHECK(accumulator.Alpha() == doctest::Approx(0.0));
	CHECK(accumulator.Accumulate(0.0) == 0);
}

TEST_CASE(
    "FixedStep: a slow frame is capped, the whole steps over the cap are dropped, and the "
    "fraction is kept")
{
	FixedStepAccumulator accumulator{kStep, 8};
	CHECK(accumulator.MaxStepsPerFrame() == 8);
	CHECK(accumulator.Accumulate(10.25 * kStep) == 8);
	CHECK(accumulator.DroppedTime() == doctest::Approx(2.0 * kStep));
	CHECK(accumulator.Alpha() == doctest::Approx(0.25));
	// The next frame starts from the kept fraction, not from the dropped steps.
	CHECK(accumulator.Accumulate(0.75 * kStep) == 1);
}

TEST_CASE("FixedStep: a delta too large for an int is still capped")
{
	FixedStepAccumulator accumulator{kStep, 8};
	CHECK(accumulator.Accumulate(1.0e12) == 8);
	CHECK(accumulator.Alpha() >= 0.0);
	CHECK(accumulator.Alpha() < 1.0);
}

TEST_CASE("FixedStep: the simulation clock's compression runs more steps, not longer ones")
{
	SimClock clock;
	clock.SetCompression(4.0);
	FixedStepAccumulator accumulator{kStep};
	const int steps = accumulator.Accumulate(clock.ScaledDelta(kStep));
	CHECK(steps == 4);
	for (int i = 0; i < steps; ++i)
		clock.Advance(accumulator.FixedStep());
	CHECK(clock.Step() == 4);
	CHECK(clock.Now() == doctest::Approx(4.0 * kStep));
}
