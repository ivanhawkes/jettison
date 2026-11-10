#pragma once

#include <cstdint>

namespace Jettison::Math
{
// Everything a generator's future output depends on, for saving it and restoring it.
struct RandomState
{
	std::uint64_t state{0};
	std::uint64_t increment{0};

	friend bool operator==(const RandomState &, const RandomState &) = default;
};

// The engine's own generator and distributions: PCG32 (XSH RR, 64-bit state), seeded as the
// reference implementation seeds it. Its output is part of the save format and the replay
// contract, so the tests pin it with the reference implementation's published values and pin the
// distributions with recorded ones: a standard library update cannot move it, and nor can this
// file without a test failing. Simulation code seeds it from simulation state and never uses
// std::random.
//
// PCG32 over xoshiro256**: half the state to save, and a stream selector, so one seed gives each
// system or ship a sequence of its own that does not overlap its neighbours'. xoshiro's speed
// does not show at the rate the simulation draws numbers.
class Random
{
  public:
	explicit Random(std::uint64_t seed = 0, std::uint64_t stream = 0);
	static Random FromState(const RandomState &state);

	std::uint32_t NextU32();
	// Two draws, the first in the high half.
	std::uint64_t NextU64();
	// Uniform in [0, 1), from the top 24 and 53 bits of a draw: every value is a multiple of the
	// smallest step, so each is equally likely.
	float NextFloat();
	double NextDouble();
	// Uniform in [min, max], without the bias a modulo has; min <= max. The whole int32 range is
	// allowed.
	std::int32_t Range(std::int32_t min, std::int32_t max);
	// Uniform in [min, max); max itself only by rounding.
	float Range(float min, float max);

	RandomState State() const
	{
		return m_state;
	}

  private:
	RandomState m_state;
};
} // namespace Jettison::Math
