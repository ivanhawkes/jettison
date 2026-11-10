#include <jettison/animation/Playback.h>

#include <algorithm>
#include <cmath>

namespace Jettison::Animation
{
namespace
{
// A turn in radians.
constexpr float kFullTurn = 6.28318531f;
} // namespace

void AdvanceRotation(RotationAnimationComponent &animation, float frameTime)
{
	animation.angle = std::fmod(animation.angle + animation.rotationSpeed * frameTime, kFullTurn);
}

void AdvancePlayer(AnimationPlayerComponent &player, float duration, float frameTime)
{
	if (!player.playing || duration <= 0.0f)
		return;
	player.time += frameTime * player.playbackSpeed;
	if (player.looping)
		player.time = std::fmod(std::max(player.time, 0.0f), duration);
	else
		player.time = std::clamp(player.time, 0.0f, duration);
}
} // namespace Jettison::Animation
