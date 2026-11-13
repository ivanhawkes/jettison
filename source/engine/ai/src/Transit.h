#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Jettison::Ai::Detail
{
// A transit in whole steps, rounded up, and at least one: a lane's and a cruise's. A millionth of a
// step is let go before rounding up, since a transit authored in seconds is rarely a whole number of
// steps in floating point: 40 s at 60 Hz is 2400.0000000000005 steps.
inline std::uint64_t TransitSteps(double transitSeconds, double fixedStep)
{
	const double steps = std::ceil((transitSeconds / fixedStep) - 1.0e-6);
	return std::max<std::uint64_t>(1, steps > 0.0 ? static_cast<std::uint64_t>(steps) : 0);
}
} // namespace Jettison::Ai::Detail
