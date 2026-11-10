#include <jettison/animation/Rig.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace Jettison::Animation
{
namespace
{
glm::mat4 LocalTransform(const RigNode &node)
{
	glm::mat4 matrix = glm::translate(glm::mat4{1.0f}, node.translation);
	matrix *= glm::mat4_cast(node.rotation);
	return glm::scale(matrix, node.scale);
}

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

glm::vec4 SampleTrack(const Track &track, float time)
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
} // namespace

bool Rig::HasAnimation(std::string_view name) const
{
	return std::any_of(clips.begin(), clips.end(), [&](const Clip &clip) { return clip.name == name; });
}

float Rig::AnimationDuration(std::string_view name) const
{
	const auto clip =
	    std::find_if(clips.begin(), clips.end(), [&](const Clip &value) { return value.name == name; });
	return clip == clips.end() ? 0.0f : clip->duration;
}

void Rig::EvaluateSkin(
    std::string_view animationName, float time, std::vector<glm::mat4> &jointMatrices,
    glm::mat4 &meshNodeTransform) const
{
	if (nodes.empty() || meshNodeIndex >= nodes.size())
		throw std::runtime_error("Cannot evaluate a skin without glTF node data");
	std::vector<RigNode> pose = nodes;
	if (!animationName.empty())
	{
		const auto clip = std::find_if(
		    clips.begin(), clips.end(), [&](const Clip &value) { return value.name == animationName; });
		if (clip == clips.end())
			throw std::runtime_error("Unknown glTF animation clip: " + std::string(animationName));
		for (const Track &track : clip->tracks)
		{
			const glm::vec4 value = SampleTrack(track, time);
			RigNode &node = pose[track.nodeIndex];
			if (track.path == TrackPath::Translation)
				node.translation = glm::vec3(value);
			else if (track.path == TrackPath::Scale)
				node.scale = glm::vec3(value);
			else
				node.rotation = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
		}
	}

	std::vector<glm::mat4> worldTransforms(nodes.size(), glm::mat4{1.0f});
	std::vector<std::uint8_t> states(nodes.size(), 0);
	std::function<const glm::mat4 &(std::size_t)> computeWorld =
	    [&](std::size_t index) -> const glm::mat4 &
	{
		if (states[index] == 2)
			return worldTransforms[index];
		if (states[index] == 1)
			throw std::runtime_error("glTF node hierarchy contains a cycle");
		states[index] = 1;
		const glm::mat4 local = LocalTransform(pose[index]);
		worldTransforms[index] =
		    pose[index].parent >= 0 ? computeWorld(static_cast<std::size_t>(pose[index].parent)) * local
		                            : local;
		states[index] = 2;
		return worldTransforms[index];
	};
	for (std::size_t index = 0; index < pose.size(); ++index)
		computeWorld(index);

	meshNodeTransform = worldTransforms[meshNodeIndex];
	const glm::mat4 inverseMeshTransform = glm::inverse(meshNodeTransform);
	jointMatrices.resize(joints.size());
	for (std::size_t index = 0; index < joints.size(); ++index)
		jointMatrices[index] = inverseMeshTransform * worldTransforms[joints[index].nodeIndex] *
		                       joints[index].inverseBindMatrix;
}
} // namespace Jettison::Animation
