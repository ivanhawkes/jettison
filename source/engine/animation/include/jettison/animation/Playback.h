#pragma once

#include <jettison/animation/Components.h>

namespace Jettison::Animation
{
// Advances a rotation animation's phase by frameTime seconds, kept inside one turn: the rotation
// is periodic, so the wrap is not visible, and an hour of running costs the precision of the
// first minute.
void AdvanceRotation(RotationAnimationComponent &animation, float frameTime);

// Advances a playing clip by frameTime seconds at its playback speed, wrapping a looping clip and
// clamping one that is not. A clip of no duration, or a paused player, does not move.
void AdvancePlayer(AnimationPlayerComponent &player, float duration, float frameTime);
} // namespace Jettison::Animation
