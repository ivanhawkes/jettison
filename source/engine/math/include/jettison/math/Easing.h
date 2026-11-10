#pragma once

namespace Jettison::Math
{
// Easing functions over t in [0, 1], for UI and camera transitions (easings.net is the reference
// for the shapes). Filled in phase 2.
enum class Easing
{
	Linear,
	InQuad,
	OutQuad,
	InOutQuad,
	InCubic,
	OutCubic,
	InOutCubic,
	OutBack,
	OutElastic
};

float Ease(Easing easing, float t);
} // namespace Jettison::Math
