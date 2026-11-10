#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Jettison::Animation
{
struct RotationAnimationComponent
{
	glm::vec3 rotationAxis{0.0f, 1.0f, 0.0f};
	float rotationSpeed{0.785398163f};

	// The phase of the rotation, owned by the frame loop and not written to the scene file. The
	// world transform adds it to the authored rotation, which therefore stays authored: a rotation
	// accumulated in TransformComponent is authored state that the running engine overwrites, and
	// a scene saved from a running engine records where the animation had got to.
	float angle{0.0f};
};

// The pose a rig's clip player produced this frame, for the renderer's joint palette.
struct SkeletonComponent
{
	std::vector<glm::mat4> jointMatrices;
	glm::mat4 meshNodeTransform{1.0f};
};

struct AnimationPlayerComponent
{
	std::string clipName;
	// The phase of the clip, owned by the frame loop and not written to the scene file.
	float time{0.0f};
	float playbackSpeed{1.0f};
	bool looping{true};
	bool playing{true};
};
} // namespace Jettison::Animation
