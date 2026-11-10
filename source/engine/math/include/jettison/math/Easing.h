#pragma once

namespace Jettison::Math
{
// Easing functions over t in [0, 1], for UI and camera transitions, with the shapes and
// constants of easings.net. Every one maps 0 to 0 and 1 to 1, and t outside [0, 1] is clamped.
// OutBack overshoots 1 and OutElastic rings around it on the way.
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
