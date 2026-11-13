#include <jettison/animation/Clip.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>

namespace Jettison::Animation
{
namespace
{
float SampleInterval(
    const std::vector<float> &times, float time, std::size_t &first, std::size_t &second)
{
	if (times.empty())
		return 0.0f;
	const auto upper = std::upper_bound(times.begin(), times.end(), time);
	if (upper == times.begin())
	{
		first = second = 0;
		return 0.0f;
	}
	if (upper == times.end())
	{
		first = second = times.size() - 1;
		return 0.0f;
	}
	second = static_cast<std::size_t>(upper - times.begin());
	first = second - 1;
	const float duration = times[second] - times[first];
	return duration > 0.0f ? (time - times[first]) / duration : 0.0f;
}
} // namespace

glm::vec4 Sample(const Track &track, float time)
{
	if (track.times.empty())
		return glm::vec4{0.0f};

	std::size_t first = 0;
	std::size_t second = 0;
	const float factor = SampleInterval(track.times, time, first, second);
	if (track.interpolation == Interpolation::CubicSpline)
	{
		const glm::vec4 start = track.values[first * 3 + 1];
		if (first == second)
			return start;
		const glm::vec4 outTangent = track.values[first * 3 + 2];
		const glm::vec4 end = track.values[second * 3 + 1];
		const glm::vec4 inTangent = track.values[second * 3];
		const float delta = track.times[second] - track.times[first];
		const float t2 = factor * factor;
		const float t3 = t2 * factor;
		return (2.0f * t3 - 3.0f * t2 + 1.0f) * start + (t3 - 2.0f * t2 + factor) * delta * outTangent +
		       (-2.0f * t3 + 3.0f * t2) * end + (t3 - t2) * delta * inTangent;
	}

	const glm::vec4 start = track.values[first];
	if (first == second || track.interpolation == Interpolation::Step)
		return start;
	const glm::vec4 end = track.values[second];
	if (track.path == TrackPath::Rotation)
	{
		const glm::quat startRotation{start.w, start.x, start.y, start.z};
		const glm::quat endRotation{end.w, end.x, end.y, end.z};
		const glm::quat rotation = glm::normalize(glm::slerp(startRotation, endRotation, factor));
		return {rotation.x, rotation.y, rotation.z, rotation.w};
	}
	return glm::mix(start, end, factor);
}

const Clip *FindClip(const std::vector<Clip> &clips, std::string_view name)
{
	const auto clip =
	    std::find_if(clips.begin(), clips.end(), [&](const Clip &value) { return value.name == name; });
	return clip == clips.end() ? nullptr : &*clip;
}
} // namespace Jettison::Animation
