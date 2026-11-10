#include <jettison/time/Clock.h>

#include <doctest/doctest.h>

#include <chrono>

using namespace Jettison::Time;
using namespace std::chrono_literals;

TEST_CASE("RealClock: a tick is the time since the previous one")
{
	RealClock clock;
	const auto start = RealClock::Clock::now();
	clock.Tick(start);
	CHECK(clock.Tick(start + 10ms).count() == doctest::Approx(0.010));
	CHECK(clock.Tick(start + 30ms).count() == doctest::Approx(0.020));
}

TEST_CASE("RealClock: a long frame is clamped, and an instant before the last is no time at all")
{
	RealClock clock{RealClock::Duration{0.1}};
	const auto start = RealClock::Clock::now();
	clock.Tick(start);
	// A breakpoint held for a minute.
	CHECK(clock.Tick(start + 60s).count() == doctest::Approx(0.1));
	CHECK(clock.Tick(start + 30s).count() == 0.0);
	CHECK(clock.MaxTick().count() == doctest::Approx(0.1));
}

TEST_CASE("RealClock: elapsed time is unclamped and does not go backwards")
{
	const RealClock clock;
	const RealClock::Duration first = clock.Elapsed();
	const RealClock::Duration second = clock.Elapsed();
	CHECK(first.count() >= 0.0);
	CHECK(second >= first);
}

TEST_CASE("SimClock: a new clock is at zero, running, in real time")
{
	const SimClock clock;
	CHECK(clock.Now() == 0.0);
	CHECK(clock.Step() == 0);
	CHECK_FALSE(clock.Paused());
	CHECK(clock.Compression() == 1.0);
}

TEST_CASE("SimClock: each step advances time by the step and counts it")
{
	SimClock clock;
	for (int i = 0; i < 60; ++i)
		clock.Advance(1.0 / 60.0);
	CHECK(clock.Step() == 60);
	CHECK(clock.Now() == doctest::Approx(1.0));
}

TEST_CASE("SimClock: pausing stops the scaled delta, and compression multiplies it")
{
	SimClock clock;
	CHECK(clock.ScaledDelta(0.5) == 0.5);
	clock.SetCompression(4.0);
	CHECK(clock.ScaledDelta(0.5) == 2.0);
	clock.SetPaused(true);
	CHECK(clock.Paused());
	CHECK(clock.ScaledDelta(0.5) == 0.0);
	// Compression survives a pause.
	clock.SetPaused(false);
	CHECK(clock.ScaledDelta(0.5) == 2.0);
}

TEST_CASE("SimClock: a paused clock can still be single-stepped")
{
	SimClock clock;
	clock.SetPaused(true);
	clock.Advance(0.25);
	CHECK(clock.Step() == 1);
	CHECK(clock.Now() == 0.25);
}
