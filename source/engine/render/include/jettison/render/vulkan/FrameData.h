#pragma once

#include <glm/glm.hpp>

#include <cstddef>

namespace Jettison::Render
{
// The C++ side of assets/shaders/include/frame.glsl. The GLSL is the one source of the layout;
// these mirror it, and the asserts below are what hold the two together, so a field added to one
// and not the other fails the build rather than the frame.
struct FrameUBO
{
	alignas(16) glm::mat4 view{1.0f};
	alignas(16) glm::mat4 projection{1.0f};
	alignas(16) glm::vec4 cameraPosition{0.0f};
	alignas(16) glm::vec4 ambientColor{0.0f};
	// x: the lights in the light list.
	alignas(16) glm::uvec4 counts{0u};
};
static_assert(sizeof(FrameUBO) == 176);
static_assert(offsetof(FrameUBO, projection) == 64);
static_assert(offsetof(FrameUBO, cameraPosition) == 128);
static_assert(offsetof(FrameUBO, ambientColor) == 144);
static_assert(offsetof(FrameUBO, counts) == 160);

// One entry of the light list, std430.
struct GpuLight
{
	glm::vec4 position{0.0f};
	glm::vec4 colorIntensity{0.0f};
};
static_assert(sizeof(GpuLight) == 32);
} // namespace Jettison::Render
