#pragma once

#include <chrono>
#include <cstdint>

namespace Jettison::Time
{
// Wall-clock time for presentation: frame deltas, UI animation, profiling. Never read by
// simulation code. Monotonic (steady_clock, which is CLOCK_MONOTONIC on Linux), so a change to the
// system time does not become a frame of minus an hour.
class RealClock
{
  public:
	using Clock = std::chrono::steady_clock;
	using Duration = std::chrono::duration<double>;

	// A quarter of a second: long enough that a slow frame is still reported as slow, short enough
	// that a breakpoint held for a minute does not become a minute of animation.
	static constexpr Duration kDefaultMaxTick{0.25};

	explicit RealClock(Duration maxTick = kDefaultMaxTick);

	// Seconds since the previous tick (or since construction), clamped to the maximum tick.
	Duration Tick();
	// The same, at a time the caller has already read: the frame loop reads the clock once per
	// frame and hands the same instant to everything that frame times.
	Duration Tick(Clock::time_point now);
	// Unclamped seconds since construction.
	Duration Elapsed() const;
	Duration MaxTick() const
	{
		return m_maxTick;
	}

  private:
	Duration m_maxTick;
	Clock::time_point m_start;
	Clock::time_point m_previous;
};

// Simulation time: paused or running, compressed by a factor, advanced in fixed steps by the
// accumulator in FixedStep.h. Everything in the simulation reads time from here, so a replay runs
// the same steps as the session that recorded it.
//
// Compression scales the real time fed to the accumulator, so it runs more fixed steps per frame,
// not longer ones: the simulation's step stays the step its physics and AI were tuned for. The
// accumulator's per-frame cap bounds how far that goes; past it, simulated time falls behind the
// requested rate and the accumulator counts what it dropped.
class SimClock
{
  public:
	// Simulation seconds since the start of the game.
	double Now() const
	{
		return m_now;
	}
	std::uint64_t Step() const
	{
		return m_step;
	}
	bool Paused() const
	{
		return m_paused;
	}
	double Compression() const
	{
		return m_compression;
	}

	void SetPaused(bool paused);
	// Where a loaded save left the clock: its step and its time, the time as the bits that were
	// saved, since the sum of the steps that made it is not step * fixedStep in floating point.
	void Restore(std::uint64_t step, double now);
	// 1 is real time. Must be positive and finite; pausing is SetPaused, not a factor of 0. Refused
	// near stations and other ships by the game, not here.
	void SetCompression(double factor);
	// A frame's real seconds as simulation seconds for the accumulator: 0 while paused, scaled by
	// the compression factor otherwise.
	double ScaledDelta(double realSeconds) const;
	// One fixed step forward. Called by the accumulator's owner once per step it hands out. Not
	// refused while paused, so a debugger can single-step a paused simulation.
	void Advance(double fixedStep);

  private:
	double m_now{0.0};
	std::uint64_t m_step{0};
	bool m_paused{false};
	double m_compression{1.0};
};
} // namespace Jettison::Time
