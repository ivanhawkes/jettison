#include <jettison/render/Clusters.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include <jettison/render/Scene.h>

namespace Jettison::Render
{
// The shaders' own source, as C++: clusters.glsl is written in the subset of GLSL that GLM accepts.
namespace Glsl
{
using namespace glm;
#include "clusters.glsl"
} // namespace Glsl

static_assert(Glsl::kClusterTilesX == kClusterTilesX);
static_assert(Glsl::kClusterTilesY == kClusterTilesY);
static_assert(Glsl::kClusterSlices == kClusterSlices);
static_assert(Glsl::kClusterCount == kClusterCount);

namespace
{
// The rounding allowance, as a share of the largest distance a test involves, and its floor. The
// GPU's pow and log are allowed a few ulps times the size of their argument, which at 24 slices is
// a few millionths of a slice's depth; ten times that leaves room for the transforms around them.
constexpr float kRelativeTolerance = 1.0e-5f;
constexpr float kAbsoluteTolerance = 1.0e-4f;

std::string DescribeCluster(std::uint32_t cluster)
{
	return std::format(
	    "cluster {} (tile {}, {}, slice {})", cluster, cluster % kClusterTilesX,
	    (cluster / kClusterTilesX) % kClusterTilesY, cluster / (kClusterTilesX * kClusterTilesY));
}

Glsl::ClusterBounds ToGlsl(const ClusterBox &box)
{
	Glsl::ClusterBounds bounds;
	bounds.minimum = box.minimum;
	bounds.maximum = box.maximum;
	return bounds;
}
} // namespace

glm::vec2 ClusterTileSize(glm::vec2 viewport)
{
	return Glsl::ClusterTileSize(viewport);
}

float ClusterSliceDepth(std::uint32_t slice, float nearPlane, float range)
{
	return Glsl::ClusterSliceDepth(slice, nearPlane, range);
}

std::uint32_t ClusterSlice(float depth, float nearPlane, float range)
{
	return Glsl::ClusterSlice(depth, nearPlane, range);
}

std::uint32_t ClusterAt(glm::vec2 pixel, float depth, const ClusterView &view)
{
	return Glsl::ClusterAt(pixel, depth, ClusterTileSize(view.viewport), view.nearPlane, view.range);
}

ClusterBox ClusterBoundsOf(std::uint32_t cluster, const ClusterView &view)
{
	const Glsl::ClusterBounds bounds = Glsl::ClusterBoundsOf(
	    cluster, view.viewport, ClusterTileSize(view.viewport), view.projectionScale, view.nearPlane,
	    view.range);
	return {bounds.minimum, bounds.maximum};
}

bool LightReachesCluster(
    const GpuLight &light, const glm::mat3 &view, const ClusterBox &bounds, float margin)
{
	// As cluster_lights.comp does it: the view matrix's rotation takes render space to view space.
	return Glsl::LightReachesCluster(
	    view * glm::vec3{light.positionRange}, light.positionRange.w,
	    light.directionType.w == Glsl::kLightSpot, view * glm::vec3{light.directionType}, light.cone.z,
	    light.cone.w, ToGlsl(bounds), margin);
}

float RangeAttenuation(float distanceSquared, float range)
{
	return Glsl::RangeAttenuation(distanceSquared, range);
}

float SpotAttenuation(const GpuLight &light, const glm::vec3 &lightToSurface)
{
	return Glsl::SpotAttenuation(
	    glm::vec3{light.directionType}, lightToSurface, light.cone.x, light.cone.y);
}

GpuLight PackLight(
    const LightComponent &light, const glm::vec3 &renderPosition, const glm::vec3 &direction)
{
	GpuLight packed;
	packed.positionRange = glm::vec4{renderPosition, light.range};
	packed.colorIntensity = glm::vec4{light.color, light.intensity};
	if (light.type != LightType::Spot)
		return packed;
	// KHR_lights_punctual's form of the cone's falloff: a scale and offset on the cosine, so the
	// fragment does one multiply-add rather than two cosines.
	const float cosInner = std::cos(light.innerConeAngle);
	const float cosOuter = std::cos(light.outerConeAngle);
	const float scale = 1.0f / std::max(0.001f, cosInner - cosOuter);
	packed.directionType = glm::vec4{direction, Glsl::kLightSpot};
	packed.cone = glm::vec4{scale, -cosOuter * scale, cosOuter, std::sin(light.outerConeAngle)};
	return packed;
}

ClusterCheck CheckClusterAssignment(
    const ClusterView &view, const glm::mat3 &viewMatrix, std::span<const GpuLight> lights,
    std::span<const glm::uvec2> grid, std::span<const std::uint32_t> indices, std::uint32_t reserved)
{
	ClusterCheck check;
	if (grid.size() < kClusterCount)
	{
		check.mismatch = std::format("the grid has {} clusters, not {}", grid.size(), kClusterCount);
		return check;
	}
	// The clusters that reserved last lost their lights, in an order the atomic adds chose, so a
	// cluster's list says nothing about the reference past this point.
	check.overflowed = reserved > indices.size();

	std::vector<std::uint8_t> listed(lights.size());
	for (std::uint32_t cluster = 0; cluster < kClusterCount; ++cluster)
	{
		const glm::uvec2 entry = grid[cluster];
		check.entries += entry.y;
		check.mostLights = std::max(check.mostLights, entry.y);
		if (entry.y != 0)
			++check.litClusters;
		if (check.overflowed || !check.mismatch.empty())
			continue;
		if (entry.x > reserved || entry.y > reserved - entry.x)
		{
			check.mismatch = std::format(
			    "{} lists entries {} to {}, past the {} reserved", DescribeCluster(cluster), entry.x,
			    entry.x + entry.y, reserved);
			continue;
		}

		std::ranges::fill(listed, std::uint8_t{0});
		for (std::uint32_t entryIndex = entry.x; entryIndex < entry.x + entry.y; ++entryIndex)
		{
			const std::uint32_t light = indices[entryIndex];
			if (light >= lights.size() || listed[light] != 0)
			{
				check.mismatch = std::format(
				    "{} lists light {} {}", DescribeCluster(cluster), light,
				    light >= lights.size() ? "of a list that is shorter" : "twice");
				break;
			}
			listed[light] = 1;
		}
		if (!check.mismatch.empty())
			continue;

		const ClusterBox bounds = ClusterBoundsOf(cluster, view);
		const glm::vec3 extent = glm::max(glm::abs(bounds.minimum), glm::abs(bounds.maximum));
		const float boxReach = std::max({extent.x, extent.y, extent.z});
		for (std::uint32_t light = 0; light < lights.size(); ++light)
		{
			const GpuLight &candidate = lights[light];
			const float margin = kRelativeTolerance * (glm::length(glm::vec3{candidate.positionRange}) +
			                                           candidate.positionRange.w + boxReach) +
			                     kAbsoluteTolerance;
			if (listed[light] != 0 && !LightReachesCluster(candidate, viewMatrix, bounds, margin))
				check.mismatch = std::format(
				    "{} lists light {}, which does not reach it", DescribeCluster(cluster), light);
			else if (listed[light] == 0 && LightReachesCluster(candidate, viewMatrix, bounds, -margin))
				check.mismatch = std::format(
				    "{} does not list light {}, which reaches it", DescribeCluster(cluster), light);
			if (!check.mismatch.empty())
				break;
		}
	}
	return check;
}
} // namespace Jettison::Render
