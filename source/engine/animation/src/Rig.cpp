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
} // namespace

bool Rig::HasAnimation(std::string_view name) const
{
	return FindClip(clips, name) != nullptr;
}

float Rig::AnimationDuration(std::string_view name) const
{
	const Clip *clip = FindClip(clips, name);
	return clip == nullptr ? 0.0f : clip->duration;
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
		const Clip *clip = FindClip(clips, animationName);
		if (clip == nullptr)
			throw std::runtime_error("Unknown glTF animation clip: " + std::string(animationName));
		for (const Track &track : clip->tracks)
		{
			const glm::vec4 value = Sample(track, time);
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
