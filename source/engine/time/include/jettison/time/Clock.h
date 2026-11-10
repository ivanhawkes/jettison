#pragma once

#include <chrono>
#include <cstdint>

namespace Jettison::Time
{
// Wall-clock time for presentation: frame deltas, UI animation, profiling. Never read by
// simulation code. Filled in phase 2.
class RealClock
{
  public:
	using Duration = std::chrono::duration<double>;

	// Seconds since the previous call, clamped so a debugger pause does not become one long frame.
	Duration Tick();
	Duration Elapsed() const;
};

// Simulation time: paused or running, compressed by a factor, advanced in fixed steps by the
// accumulator in FixedStep.h. Everything in the simulation reads time from here, so a replay runs
// the same steps as the session that recorded it. Filled in phase 2.
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
	// 1 is real time. Refused near stations and other ships by the game, not here.
	void SetCompression(double factor);
	// One fixed step forward. Called by the accumulator only.
	void Advance(double fixedStep);

  private:
	double m_now{0.0};
	std::uint64_t m_step{0};
	bool m_paused{false};
	double m_compression{1.0};
};
} // namespace Jettison::Time
