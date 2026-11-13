#include <jettison/animation/Playback.h>

#include <algorithm>
#include <cmath>

namespace Jettison::Animation
{
namespace
{
// A turn in radians.
constexpr float kFullTurn = 6.28318531f;

// The events between from and to, each end included or not, in the order met going from one to
// the other.
void Cross(
    const Clip &clip, float from, float to, bool includeFrom, bool includeTo,
    std::vector<std::size_t> &crossed)
{
	const std::size_t count = clip.events.size();
	const auto within = [&](float time)
	{
		const float low = std::min(from, to);
		const float high = std::max(from, to);
		if (time < low || time > high)
			return false;
		if (time == from && !includeFrom)
			return false;
		return time != to || includeTo;
	};
	if (to >= from)
	{
		for (std::size_t index = 0; index < count; ++index)
			if (within(clip.events[index].time))
				crossed.push_back(index);
	}
	else
		for (std::size_t index = count; index-- > 0;)
			if (within(clip.events[index].time))
				crossed.push_back(index);
}
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

float AdvanceClip(
    const Clip &clip, float time, float delta, bool looping, std::vector<std::size_t> &crossed)
{
	const float duration = clip.duration;
	if (!(duration > 0.0f) || !std::isfinite(time))
		return 0.0f;
	if (looping)
	{
		time = std::fmod(time, duration);
		if (time < 0.0f)
			time += duration;
	}
	else
		time = std::clamp(time, 0.0f, duration);
	if (delta == 0.0f || !std::isfinite(delta))
		return time;

	if (!looping)
	{
		const float to = std::clamp(time + delta, 0.0f, duration);
		// The end reached includes the event there; anywhere short of it, the next advance has it.
		const bool reachedEnd = delta > 0.0f ? to == duration : to == 0.0f;
		if (to != time)
			Cross(clip, time, to, true, reachedEnd, crossed);
		return to;
	}

	if (std::abs(delta) >= duration)
	{
		// Passed over more than once: each event once, in the order met from where the clip was.
		const float end = delta > 0.0f ? duration : 0.0f;
		const float start = delta > 0.0f ? 0.0f : duration;
		Cross(clip, time, end, true, true, crossed);
		Cross(clip, start, time, true, false, crossed);
		const float to = std::fmod(time + delta, duration);
		return to < 0.0f ? to + duration : to;
	}
	const float to = time + delta;
	if (to >= 0.0f && to < duration)
	{
		Cross(clip, time, to, true, false, crossed);
		return to;
	}
	// Wrapped once, through the end and the start, which are one moment.
	if (delta > 0.0f)
	{
		Cross(clip, time, duration, true, true, crossed);
		Cross(clip, 0.0f, to - duration, true, false, crossed);
		return to - duration;
	}
	Cross(clip, time, 0.0f, true, true, crossed);
	Cross(clip, duration, to + duration, true, false, crossed);
	return to + duration;
}
} // namespace Jettison::Animation
