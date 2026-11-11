#pragma once

#include <glm/glm.hpp>

#include <cstddef>

namespace Jettison::Render
{
// The C++ side of assets/shaders/include/frame.glsl and sky.glsl. The GLSL is the one source of the
// layout; these mirror it, and the asserts below are what hold the two together, so a field added
// to one and not the other fails the build rather than the frame. Every position in them is in
// render space, metres from the camera, so the camera's own position is the origin and is not
// passed.

// sky.glsl's Sky: the same bytes under std140 and std430, being vec4s only. Render::PackSky
// (render/vulkan/Environment.h) fills it from a SkyComponent.
struct GpuSky
{
	// x: the star lattice's radius in cells, y: the brightest star's flux, z: the widest a star is
	// drawn, in radians, w: one when there are stars.
	alignas(16) glm::vec4 stars{0.0f};
	// xyz: the galactic plane's normal, w: the band's angular half-width.
	alignas(16) glm::vec4 band{0.0f, 1.0f, 0.0f, 0.1f};
	alignas(16) glm::vec4 bandColor{0.0f};
	// rgb: radiance, a: repeats around the sky.
	alignas(16) glm::vec4 nebula{0.0f};
	alignas(16) glm::uvec4 seed{0u};
};
static_assert(sizeof(GpuSky) == 80);

struct FrameUBO
{
	alignas(16) glm::mat4 view{1.0f};
	alignas(16) glm::mat4 projection{1.0f};
	// Towards the sun, normalised, in render space; w is 1 when the scene has one.
	alignas(16) glm::vec4 sunDirection{0.0f};
	alignas(16) glm::vec4 sunColorIntensity{0.0f};
	// x: the cosine of the sun's angular radius, y: the radius in radians, z: the disc's radiance
	// per unit of the sun's colour.
	alignas(16) glm::vec4 sunDisc{0.0f};
	// x: the environment's roughest specular level, y: the angle a pixel covers at the view's
	// centre, in radians.
	alignas(16) glm::vec4 environment{0.0f};
	GpuSky sky{};
	// x: the lights in the light list, y: the debug view (Render::DebugView).
	alignas(16) glm::uvec4 counts{0u};
	// The cluster grid (clusters.glsl): x the near plane, y the cluster range, as view depths.
	alignas(16) glm::vec4 clusterDepth{0.0f};
	// xy: a cluster tile's size in pixels, zw: the viewport's size in pixels.
	alignas(16) glm::vec4 clusterTile{0.0f};
};
static_assert(sizeof(FrameUBO) == 320);
static_assert(offsetof(FrameUBO, projection) == 64);
static_assert(offsetof(FrameUBO, sunDirection) == 128);
static_assert(offsetof(FrameUBO, sunColorIntensity) == 144);
static_assert(offsetof(FrameUBO, sunDisc) == 160);
static_assert(offsetof(FrameUBO, environment) == 176);
static_assert(offsetof(FrameUBO, sky) == 192);
static_assert(offsetof(FrameUBO, counts) == 272);
static_assert(offsetof(FrameUBO, clusterDepth) == 288);
static_assert(offsetof(FrameUBO, clusterTile) == 304);

// One entry of the light list, std430: frame.glsl's Light. Render::PackLight (render/Clusters.h)
// fills it from a LightComponent.
struct GpuLight
{
	// xyz: render-space position, w: range in metres.
	glm::vec4 positionRange{0.0f};
	glm::vec4 colorIntensity{0.0f};
	// xyz: the way a spot points, w: 0 for a point light, 1 for a spot.
	glm::vec4 directionType{0.0f};
	// x, y: the spot's angular falloff as a scale and offset on the cosine; z, w: the outer angle's
	// cosine and sine.
	glm::vec4 cone{0.0f, 1.0f, -1.0f, 0.0f};
};
static_assert(sizeof(GpuLight) == 64);
} // namespace Jettison::Render
