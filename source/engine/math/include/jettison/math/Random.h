#pragma once

#include <cstdint>

namespace Jettison::Math
{
// The engine's own generator and distributions. Its output is part of the save format and the
// replay contract, so phase 2 pins it with recorded reference values: a standard library update
// cannot move it. Simulation code seeds it from simulation state and never uses std::random.
// The algorithm (PCG32 or xoshiro256**) is chosen in phase 2.
class Random
{
  public:
	explicit Random(std::uint64_t seed = 0);

	std::uint32_t NextU32();
	std::uint64_t NextU64();
	// Uniform in [0, 1).
	float NextFloat();
	double NextDouble();
	// Uniform in [min, max].
	std::int32_t Range(std::int32_t min, std::int32_t max);
	float Range(float min, float max);

	std::uint64_t State() const
	{
		return m_state;
	}

  private:
	std::uint64_t m_state{0};
};
} // namespace Jettison::Math
