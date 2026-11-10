#include <jettison/math/Random.h>

#include <jettison/core/Assert.h>

namespace Jettison::Math
{
namespace
{
// The 64-bit LCG multiplier PCG32 advances by, from the reference implementation.
constexpr std::uint64_t kMultiplier = 6364136223846793005ULL;
} // namespace

// pcg32_srandom_r: the increment must be odd, and the seed is mixed in between two steps so that
// nearby seeds do not start on nearby states.
Random::Random(std::uint64_t seed, std::uint64_t stream) : m_state{0, (stream << 1U) | 1U}
{
	NextU32();
	m_state.state += seed;
	NextU32();
}

Random Random::FromState(const RandomState &state)
{
	JETTISON_VERIFY((state.increment & 1U) == 1U);
	Random random;
	random.m_state = state;
	return random;
}

// pcg32_random_r: output from the old state, so the multiply for the next one overlaps the
// permutation.
std::uint32_t Random::NextU32()
{
	const std::uint64_t previous = m_state.state;
	m_state.state = previous * kMultiplier + m_state.increment;
	const auto shifted = static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
	const auto rotation = static_cast<std::uint32_t>(previous >> 59U);
	return (shifted >> rotation) | (shifted << ((0U - rotation) & 31U));
}

std::uint64_t Random::NextU64()
{
	const std::uint64_t high = NextU32();
	return (high << 32U) | NextU32();
}

float Random::NextFloat()
{
	return static_cast<float>(NextU32() >> 8U) * 0x1.0p-24f;
}

double Random::NextDouble()
{
	return static_cast<double>(NextU64() >> 11U) * 0x1.0p-53;
}

// Lemire's multiply-and-shift: the high half of draw * span is in [0, span), and rejecting the
// draws whose low half falls under 2^32 mod span leaves every result equally likely. The modulo
// is only computed when a draw lands in the region that might need rejecting.
std::int32_t Random::Range(std::int32_t min, std::int32_t max)
{
	JETTISON_ASSERT(min <= max);
	// In unsigned arithmetic, where the span of the whole int32 range wraps to 0.
	const std::uint32_t span = static_cast<std::uint32_t>(max) - static_cast<std::uint32_t>(min) + 1U;
	if (span == 0U)
		return static_cast<std::int32_t>(NextU32());

	std::uint64_t product = static_cast<std::uint64_t>(NextU32()) * span;
	auto low = static_cast<std::uint32_t>(product);
	if (low < span)
	{
		const std::uint32_t threshold = (0U - span) % span;
		while (low < threshold)
		{
			product = static_cast<std::uint64_t>(NextU32()) * span;
			low = static_cast<std::uint32_t>(product);
		}
	}
	return static_cast<std::int32_t>(
	    static_cast<std::uint32_t>(min) + static_cast<std::uint32_t>(product >> 32U));
}

float Random::Range(float min, float max)
{
	JETTISON_ASSERT(min <= max);
	return min + (max - min) * NextFloat();
}
} // namespace Jettison::Math
