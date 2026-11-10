#pragma once

#include <glm/glm.hpp>

#include <cstddef>

namespace Jettison::Render
{
// The C++ side of assets/shaders/include/frame.glsl. The GLSL is the one source of the layout;
// these mirror it, and the asserts below are what hold the two together, so a field added to one
// and not the other fails the build rather than the frame. Every position in them is in render
// space, metres from the camera, so the camera's own position is the origin and is not passed.
struct FrameUBO
{
	alignas(16) glm::mat4 view{1.0f};
	alignas(16) glm::mat4 projection{1.0f};
	alignas(16) glm::vec4 ambientColor{0.0f};
	// x: the lights in the light list.
	alignas(16) glm::uvec4 counts{0u};
};
static_assert(sizeof(FrameUBO) == 160);
static_assert(offsetof(FrameUBO, projection) == 64);
static_assert(offsetof(FrameUBO, ambientColor) == 128);
static_assert(offsetof(FrameUBO, counts) == 144);

// One entry of the light list, std430.
struct GpuLight
{
	glm::vec4 position{0.0f};
	glm::vec4 colorIntensity{0.0f};
};
static_assert(sizeof(GpuLight) == 32);
} // namespace Jettison::Render
