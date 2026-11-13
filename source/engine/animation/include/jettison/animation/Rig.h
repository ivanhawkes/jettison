#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

#include <jettison/animation/Clip.h>

namespace Jettison::Animation
{
// One node of a rig's hierarchy in its rest pose. parent is an index into Rig::nodes, or -1 for a
// root.
struct RigNode
{
	int parent{-1};
	glm::vec3 translation{0.0f};
	glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 scale{1.0f};
};

struct SkinJoint
{
	std::size_t nodeIndex{};
	glm::mat4 inverseBindMatrix{1.0f};
};

// A mechanical rig: a node hierarchy, the joints of the skin bound to it, and the clips that move
// it. The clip player that used to live inside the Vulkan Model, moved here unchanged in phase 1
// so that sampling a clip needs no device. The model loader fills it in from glTF; nothing in it
// knows the file format. Its clips are sampled as a clip file's are (Clip.h), which move scene
// nodes rather than a rig's (ClipFile.h); glTF has no place for events, so a rig's clips have none.
struct Rig
{
	std::vector<RigNode> nodes;
	std::vector<SkinJoint> joints;
	std::vector<Clip> clips;
	// The node carrying the mesh the skin deforms. Joint matrices are relative to it.
	std::size_t meshNodeIndex{};

	bool HasSkin() const
	{
		return !joints.empty();
	}
	bool HasSceneGraph() const
	{
		return !nodes.empty();
	}
	bool HasAnimation(std::string_view name) const;

	// The clip's duration in seconds, or zero for a clip the rig does not have.
	float AnimationDuration(std::string_view name) const;

	// Poses the rig at time seconds into the named clip, or at rest for an empty name, and writes
	// one matrix per joint, in mesh-node space, and the mesh node's own transform. Throws for a
	// clip the rig does not have and for a hierarchy with a cycle: both are load-time errors that
	// reached the frame loop.
	void EvaluateSkin(
	    std::string_view animationName, float time, std::vector<glm::mat4> &jointMatrices,
	    glm::mat4 &meshNodeTransform) const;
};
} // namespace Jettison::Animation
