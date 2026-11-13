#pragma once

#include <cstddef>
#include <vector>

#include <jettison/animation/Clip.h>
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

// A clip's time moved by delta seconds (its speed by the frame's time, so negative plays it
// backwards), wrapped if it loops and held at an end if not, and the index of each event crossed
// on the way appended to crossed, in the order they were met. Returns the new time.
//
// Playing forwards an event fires as the time reaches it, so one at the start fires on the first
// advance from there; one at the end of a clip that does not loop fires as the end is reached,
// and not again while the clip is held there. Backwards is the mirror: an event fires as the time
// comes back to it, and one at the start as the start is reached. A looping clip's end is its
// start, so an event at either fires as it wraps. A clip passed over more than once in one advance
// fires each event once.
//
// A time outside the clip is taken to the nearer end, or wrapped, before the advance, firing
// nothing: setting a player's time past its clip's end shows the rig at the end without the events
// that would have brought it there.
float AdvanceClip(
    const Clip &clip, float time, float delta, bool looping, std::vector<std::size_t> &crossed);
} // namespace Jettison::Animation
