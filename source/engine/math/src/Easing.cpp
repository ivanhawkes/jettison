#include <jettison/math/Easing.h>

#include <algorithm>
#include <cmath>
#include <numbers>

#include <jettison/core/Assert.h>

namespace Jettison::Math
{
float Ease(Easing easing, float t)
{
	t = std::clamp(t, 0.0f, 1.0f);
	switch (easing)
	{
		case Easing::Linear: return t;
		case Easing::InQuad: return t * t;
		case Easing::OutQuad: return 1.0f - (1.0f - t) * (1.0f - t);
		case Easing::InOutQuad:
			return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
		case Easing::InCubic: return t * t * t;
		case Easing::OutCubic: return 1.0f - std::pow(1.0f - t, 3.0f);
		case Easing::InOutCubic:
			return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
		case Easing::OutBack:
		{
			constexpr float overshoot = 1.70158f;
			return 1.0f + (overshoot + 1.0f) * std::pow(t - 1.0f, 3.0f) +
			       overshoot * std::pow(t - 1.0f, 2.0f);
		}
		case Easing::OutElastic:
		{
			// The formula is exact at 0 only in the limit, so both ends are pinned.
			if (t == 0.0f || t == 1.0f)
				return t;
			constexpr float period = 2.0f * std::numbers::pi_v<float> / 3.0f;
			return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * period) + 1.0f;
		}
	}
	JETTISON_VERIFY(false && "unknown Easing");
	return t;
}
} // namespace Jettison::Math
