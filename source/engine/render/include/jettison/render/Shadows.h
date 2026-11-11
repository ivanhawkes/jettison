#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

#include <jettison/render/Camera.h>

namespace Jettison::Render
{
// The sun's cascaded shadow maps (docs/architecture.md, "Sun shadows"), fitted on the CPU each
// frame. The cascade selection, the blend between cascades and the map lookup are
// assets/shaders/include/shadows.glsl compiled as C++, the source the lighting include runs, so the
// render suite checks the fit against what the GPU will select.
//
// A cascade is stable when a still caster lands on the same texels from one frame to the next,
// however the camera turns or moves. Two things make it so. Each cascade's map covers the sphere
// around its slice of the view, whose radius depends only on the projection, so turning the camera
// never changes a texel's size. And the map's centre is snapped to whole texels in the sun's frame,
// measured from the camera's frame origin (CameraView::frameOrigin) in double precision rather
// than from the camera: the station an orbit camera circles moves at kilometres a second through
// the system, and a grid fixed to the system would slide under it by a fraction of a texel every
// frame.

inline constexpr std::uint32_t kShadowCascades = 4;

// Texels kept clear at each edge of a cascade's map, around the sphere it covers: half a texel for
// the snap, one and a half for the filter's taps, and the rest for the normal offset the lighting
// include samples at.
inline constexpr float kShadowBorderTexels = 4.0f;

struct ShadowSettings
{
	// The view depth at which the last cascade ends, in metres. Past it the sun casts no shadow.
	float distance{400.0f};
	// The practical split scheme's blend, from even splits (0) to logarithmic ones (1).
	float splitBlend{0.8f};
	// Each cascade's map is this many texels on a side, a power of two.
	std::uint32_t resolution{2048};
};

struct ShadowCascade
{
	// Render space to the map: x and y its NDC, z the reversed depth (shadows.glsl).
	glm::mat4 matrix{1.0f};
	// The view depth at which the cascade ends.
	float split{0.0f};
	// The sphere around the cascade's slice of the view, and a texel's size, both in metres.
	float radius{0.0f};
	float texelSize{0.0f};
};

struct SunShadows
{
	std::array<ShadowCascade, kShadowCascades> cascades{};

	glm::vec4 Splits() const;
	glm::vec4 TexelSizes() const;
};

// The cascades' far depths: the practical split scheme between the near plane and the shadow
// distance, which is never less than twice the near plane. The last is the distance.
std::array<float, kShadowCascades> ShadowSplits(float nearPlane, const ShadowSettings &settings);

// The cascades for a camera with this aspect, and the sun towards `towardsSun` in the system frame,
// unit length. Each covers the view depths that select it, from ShadowCascadeStart to its split.
SunShadows FitSunShadows(
    const CameraView &camera, float aspect, const glm::dvec3 &towardsSun,
    const ShadowSettings &settings);

// shadows.glsl, as the lighting include runs it.
std::uint32_t ShadowCascadeAt(float depth, const glm::vec4 &splits);
float ShadowCascadeStart(std::uint32_t cascade, const glm::vec4 &splits, float nearPlane);
float ShadowCascadeBlend(float depth, float split);
glm::vec3 ShadowMapCoordinate(const glm::mat4 &cascade, const glm::vec3 &renderPosition);
} // namespace Jettison::Render
