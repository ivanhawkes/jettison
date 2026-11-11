#include <jettison/render/Shadows.h>

#include <algorithm>
#include <cmath>

namespace Jettison::Render
{
// The shaders' own source, as C++: shadows.glsl is written in the subset of GLSL that GLM accepts.
namespace Glsl
{
using namespace glm;
#include "shadows.glsl"
} // namespace Glsl

static_assert(Glsl::kShadowCascades == kShadowCascades);

glm::vec4 SunShadows::Splits() const
{
	return {cascades[0].split, cascades[1].split, cascades[2].split, cascades[3].split};
}

glm::vec4 SunShadows::TexelSizes() const
{
	return {cascades[0].texelSize, cascades[1].texelSize, cascades[2].texelSize, cascades[3].texelSize};
}

std::array<float, kShadowCascades> ShadowSplits(float nearPlane, const ShadowSettings &settings)
{
	const double nearDepth = nearPlane;
	const double distance = std::max(double{settings.distance}, 2.0 * nearDepth);
	const double blend = std::clamp(double{settings.splitBlend}, 0.0, 1.0);
	std::array<float, kShadowCascades> splits{};
	for (std::uint32_t cascade = 0; cascade < kShadowCascades; ++cascade)
	{
		const double share = double(cascade + 1) / double(kShadowCascades);
		const double logarithmic = nearDepth * std::pow(distance / nearDepth, share);
		const double even = nearDepth + (distance - nearDepth) * share;
		splits[cascade] = static_cast<float>(blend * logarithmic + (1.0 - blend) * even);
	}
	// Exactly, rather than as the formula rounds it.
	splits.back() = static_cast<float>(distance);
	return splits;
}

SunShadows FitSunShadows(
    const CameraView &camera, float aspect, const glm::dvec3 &towardsSun,
    const ShadowSettings &settings)
{
	const auto splits = ShadowSplits(camera.nearPlane, settings);
	const glm::vec4 splitVector{splits[0], splits[1], splits[2], splits[3]};

	// The sun's frame: z towards it, x and y across the maps. Built from the system's axes, never the
	// camera's, so a camera that turns does not turn the texel grid.
	const glm::dvec3 lightZ = glm::normalize(towardsSun);
	const glm::dvec3 reference =
	    std::abs(lightZ.y) < 0.99 ? glm::dvec3{0.0, 1.0, 0.0} : glm::dvec3{1.0, 0.0, 0.0};
	const glm::dvec3 lightX = glm::normalize(glm::cross(reference, lightZ));
	const glm::dvec3 lightY = glm::cross(lightZ, lightX);

	// A slice of the view between two depths is symmetric about the view axis, and the corners of
	// its cross-section at depth d are d * slope from the axis.
	const double tanHalf = std::tan(double{camera.verticalFov} * 0.5);
	const double slopeSquared = tanHalf * tanHalf * (1.0 + double{aspect} * double{aspect});
	const glm::dvec3 forward = camera.orientation * glm::dvec3{0.0, 0.0, -1.0};
	// The frame origin in render space, taken in double precision: both are system positions.
	const glm::dvec3 origin = camera.frameOrigin - camera.position;
	const double resolution = settings.resolution;

	SunShadows shadows;
	for (std::uint32_t index = 0; index < kShadowCascades; ++index)
	{
		ShadowCascade &cascade = shadows.cascades[index];
		const double nearDepth = Glsl::ShadowCascadeStart(index, splitVector, camera.nearPlane);
		const double farDepth = splits[index];
		// The smallest sphere through the slice's eight corners is centred on the axis where the
		// near and far corners are equally far off, or at the far cap when that is further.
		const double along = std::min((farDepth + nearDepth) * (1.0 + slopeSquared) * 0.5, farDepth);
		const double radius = std::max(
		    std::sqrt(nearDepth * nearDepth * slopeSquared + (along - nearDepth) * (along - nearDepth)),
		    std::sqrt(farDepth * farDepth * slopeSquared + (farDepth - along) * (farDepth - along)));
		// The map covers the sphere and its border. Half the map is then exactly resolution / 2
		// texels, a power of two, so a centre on whole texels puts every texel edge on them too.
		const double halfExtent = radius * resolution / (resolution - 2.0 * kShadowBorderTexels);
		const double texel = 2.0 * halfExtent / resolution;

		// The sphere's centre, from the frame origin, snapped across the map to whole texels. Its
		// depth along the sun is not snapped: the depths stored shift with it, and so do the
		// receivers' compared with them.
		const glm::dvec3 fromOrigin = forward * along - origin;
		const double acrossX = std::round(glm::dot(fromOrigin, lightX) / texel) * texel;
		const double acrossY = std::round(glm::dot(fromOrigin, lightY) / texel) * texel;
		const glm::dvec3 centre =
		    origin + lightX * acrossX + lightY * acrossY + lightZ * glm::dot(fromOrigin, lightZ);

		// Orthographic along the sun: x and y over the map's half extent, and depth from 1 at the
		// sun's side of the sphere to 0 at the far side. A caster further towards the sun than the
		// map reaches is clamped to 1 by the shadow pass's depth clamp, and still shadows.
		glm::dmat4 matrix{1.0};
		const double depthScale = 0.5 / halfExtent;
		for (int axis = 0; axis < 3; ++axis)
		{
			matrix[axis][0] = lightX[axis] / halfExtent;
			matrix[axis][1] = lightY[axis] / halfExtent;
			matrix[axis][2] = lightZ[axis] * depthScale;
			matrix[axis][3] = 0.0;
		}
		matrix[3][0] = -glm::dot(centre, lightX) / halfExtent;
		matrix[3][1] = -glm::dot(centre, lightY) / halfExtent;
		matrix[3][2] = 0.5 - glm::dot(centre, lightZ) * depthScale;
		matrix[3][3] = 1.0;

		cascade.matrix = glm::mat4{matrix};
		cascade.split = static_cast<float>(farDepth);
		cascade.radius = static_cast<float>(radius);
		cascade.texelSize = static_cast<float>(texel);
	}
	return shadows;
}

std::uint32_t ShadowCascadeAt(float depth, const glm::vec4 &splits)
{
	return Glsl::ShadowCascadeAt(depth, splits);
}

float ShadowCascadeStart(std::uint32_t cascade, const glm::vec4 &splits, float nearPlane)
{
	return Glsl::ShadowCascadeStart(cascade, splits, nearPlane);
}

float ShadowCascadeBlend(float depth, float split)
{
	return Glsl::ShadowCascadeBlend(depth, split);
}

glm::vec3 ShadowMapCoordinate(const glm::mat4 &cascade, const glm::vec3 &renderPosition)
{
	return Glsl::ShadowMapCoordinate(cascade, renderPosition);
}
} // namespace Jettison::Render
