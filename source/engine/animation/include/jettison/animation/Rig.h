#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Animation
{
enum class TrackPath
{
	Translation,
	Rotation,
	Scale
};

enum class Interpolation
{
	Linear,
	Step,
	CubicSpline
};

// One node of a rig's hierarchy in its rest pose. parent is an index into Rig::nodes, or -1 for a
// root.
struct RigNode
{
	int parent{-1};
	glm::vec3 translation{0.0f};
	glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec3 scale{1.0f};
};

// Keyframes for one property of one node. Rotations are stored x, y, z, w; translations and
// scales use the first three components. A cubic-spline track holds three values per key, in-tangent,
// value and out-tangent, as glTF lays them out.
struct Track
{
	std::size_t nodeIndex{};
	TrackPath path{TrackPath::Translation};
	Interpolation interpolation{Interpolation::Linear};
	std::vector<float> times;
	std::vector<glm::vec4> values;
};

struct Clip
{
	std::string name;
	float duration{};
	std::vector<Track> tracks;
};

struct SkinJoint
{
	std::size_t nodeIndex{};
	glm::mat4 inverseBindMatrix{1.0f};
};

// A mechanical rig: a node hierarchy, the joints of the skin bound to it, and the clips that move
// it. The clip player that used to live inside the Vulkan Model, moved here unchanged in phase 1
// so that sampling a clip needs no device. The model loader fills it in from glTF; nothing in it
// knows the file format. Phase 10 generalises it to keyframed TRS tracks on any scene node and
// adds clip events.
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
