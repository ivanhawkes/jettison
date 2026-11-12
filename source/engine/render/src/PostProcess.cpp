#include <jettison/render/PostProcess.h>

#include <algorithm>

#include <jettison/scene/Components.h>

namespace Jettison::Render
{
// The shaders' own source, as C++: post.glsl is written in the subset of GLSL that GLM accepts.
namespace Glsl
{
using namespace glm;
#include "post.glsl"
} // namespace Glsl

static_assert(Glsl::kNoSeed == 0xffffu);
static_assert(Glsl::kBloomLevels == int{kBloomLevels});
static_assert(Glsl::kFloodSteps == int{kFloodSteps});

std::uint8_t StencilBits(const Scene::MaskComponent *mask, bool outlined)
{
	std::uint32_t bits = outlined ? kStencilOutlined : 0u;
	if (mask != nullptr)
	{
		if (mask->cockpit)
			bits |= kStencilCockpit;
		if (mask->unprocessed)
			bits |= kStencilUnprocessed;
		bits |= (std::uint32_t{mask->game} & 31u) << kStencilGameShift;
	}
	return static_cast<std::uint8_t>(bits);
}

std::uint32_t ObjectId(std::uint32_t draw, std::uint32_t outline)
{
	const std::uint32_t object = draw < kMaxObjectIds ? draw + 1 : 0u;
	const std::uint32_t mark = outline < kMaxOutlines ? outline + 1 : 0u;
	return object | (mark << 16);
}

std::vector<glm::uvec2> BloomExtents(glm::uvec2 frame)
{
	std::vector<glm::uvec2> extents;
	glm::uvec2 extent = frame;
	for (int level = 0; level < Glsl::kBloomLevels; ++level)
	{
		extent = glm::max(extent / 2u, glm::uvec2{1u});
		extents.push_back(extent);
	}
	return extents;
}

namespace Post
{
int AoSamples()
{
	return Glsl::kAoSamples;
}

glm::vec3 ViewPositionAt(
    glm::vec2 pixel, float depth, glm::vec2 viewport, glm::vec2 projectionScale, float nearPlane)
{
	return Glsl::ViewPositionAt(pixel, depth, viewport, projectionScale, nearPlane);
}

float ScreenRadius(float radius, float viewDepth, float viewportHeight, float verticalScale)
{
	return Glsl::ScreenRadius(radius, viewDepth, viewportHeight, verticalScale);
}

glm::vec3 AoSpiralSample(int index, float angle)
{
	return Glsl::AoSpiralSample(index, angle);
}

float AoPixelAngle(int x, int y)
{
	return Glsl::AoPixelAngle(x, y);
}

float ObscuranceSample(glm::vec3 p, glm::vec3 n, glm::vec3 q, float radius)
{
	return Glsl::ObscuranceSample(p, n, q, radius);
}

float AmbientAccess(float sum, float samples, float radius, float intensity)
{
	return Glsl::AmbientAccess(sum, samples, radius, intensity);
}

float SpecularAccess(float nDotV, float access, float roughness)
{
	return Glsl::SpecularAccess(nDotV, access, roughness);
}

float AoBlurWeight(float depth, float neighbourDepth)
{
	return Glsl::AoBlurWeight(depth, neighbourDepth);
}

int BloomLevels()
{
	return Glsl::kBloomLevels;
}

glm::vec3 BloomDownsample(const glm::vec3 (&taps)[13], bool karis)
{
	return Glsl::BloomDownsample(
	    taps[0], taps[1], taps[2], taps[3], taps[4], taps[5], taps[6], taps[7], taps[8], taps[9],
	    taps[10], taps[11], taps[12], karis);
}

glm::vec3 BloomUpsample(const glm::vec3 (&taps)[9])
{
	return Glsl::BloomUpsample(
	    taps[0], taps[1], taps[2], taps[3], taps[4], taps[5], taps[6], taps[7], taps[8]);
}

glm::vec3 ApplyBloom(glm::vec3 frame, glm::vec3 bloomTop, float strength)
{
	return Glsl::ApplyBloom(frame, bloomTop, strength);
}

std::uint32_t NoSeed()
{
	return Glsl::kNoSeed;
}

int FloodSteps()
{
	return Glsl::kFloodSteps;
}

int FloodStride(int step)
{
	return Glsl::FloodStride(step);
}

glm::uvec2 NearerSeed(glm::uvec2 pixel, glm::uvec2 best, glm::uvec2 candidate)
{
	return Glsl::NearerSeed(pixel, best, candidate);
}

float GlowFalloff(float distance, float width)
{
	return Glsl::GlowFalloff(distance, width);
}

std::uint32_t ObjectOutline(float objectId)
{
	return Glsl::ObjectOutline(objectId);
}
} // namespace Post
} // namespace Jettison::Render
