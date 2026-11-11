#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>

#include <jettison/render/vulkan/FrameData.h>

namespace Jettison::Render
{
struct LightComponent;

// The cluster grid of docs/architecture.md ("The grid and the infinite far plane") on the CPU. The
// functions are assets/shaders/include/clusters.glsl compiled as C++, the same source the cluster
// programs and the lighting include run on the GPU, so the CPU reference below and the render
// suite's tests check that source rather than a copy of it.

inline constexpr std::uint32_t kClusterTilesX = 32;
inline constexpr std::uint32_t kClusterTilesY = 18;
inline constexpr std::uint32_t kClusterSlices = 48;
inline constexpr std::uint32_t kClusterCount = kClusterTilesX * kClusterTilesY * kClusterSlices;

// What a frame's grid is made from: the frame constants' share of it.
struct ClusterView
{
	// The viewport, in pixels.
	glm::vec2 viewport{1.0f};
	// The projection matrix's [0][0] and [1][1].
	glm::vec2 projectionScale{1.0f};
	// The grid's depth range: the camera's near plane to the cluster range, view depths in metres.
	float nearPlane{0.1f};
	float range{1.0f};

	friend bool operator==(const ClusterView &, const ClusterView &) = default;
};

// A cluster's bounds in view space (x right, y up, looking down -z).
struct ClusterBox
{
	glm::vec3 minimum{0.0f};
	glm::vec3 maximum{0.0f};
};

glm::vec2 ClusterTileSize(glm::vec2 viewport);
// The view depth slice `slice` begins at.
float ClusterSliceDepth(std::uint32_t slice, float nearPlane, float range);
// The slice a view depth is in, or kClusterSlices outside the grid.
std::uint32_t ClusterSlice(float depth, float nearPlane, float range);
// The cluster a framebuffer position at a view depth is in, or kClusterCount outside the grid.
std::uint32_t ClusterAt(glm::vec2 pixel, float depth, const ClusterView &view);
ClusterBox ClusterBoundsOf(std::uint32_t cluster, const ClusterView &view);
// Whether the light, in render space, can reach the cluster; the view matrix turns it into view
// space. The margin widens the light by that many metres.
bool LightReachesCluster(
    const GpuLight &light, const glm::mat3 &view, const ClusterBox &bounds, float margin = 0.0f);
// The falloffs the lighting include applies: windowed inverse square over the distance squared,
// and a spot's across its cone, with the light's packed scale and offset.
float RangeAttenuation(float distanceSquared, float range);
float SpotAttenuation(const GpuLight &light, const glm::vec3 &lightToSurface);

// A light as the light list carries it: its position in render space, and for a spot the way it
// points, unit length, in the same space.
GpuLight PackLight(
    const LightComponent &light, const glm::vec3 &renderPosition, const glm::vec3 &direction);

// The GPU's assignment for one frame, read back, against the CPU's from the same inputs. Each
// cluster's lights are compared as sets with a tolerance: every light that reaches the cluster with
// a margin to spare has to be listed, and every light listed has to reach it within the margin,
// which is wide enough for the GPU's rounding and no wider. The margin is a millionth of the far
// distances involved: a light a centimetre clear of a cluster 2 km out is still a disagreement.
struct ClusterCheck
{
	// The clusters that list a light, and the entries listed, over the whole grid.
	std::uint32_t litClusters{0};
	std::uint32_t entries{0};
	// The most lights any one cluster lists.
	std::uint32_t mostLights{0};
	// The index list ran out: the GPU reserved more entries than it holds.
	bool overflowed{false};
	// The first cluster that disagrees, described; empty when they agree.
	std::string mismatch;

	bool Agrees() const
	{
		return !overflowed && mismatch.empty();
	}
};

// What the cluster check has found over a run (Renderer::SetClusterCheck).
struct ClusterCheckTotals
{
	// The frames whose assignment was read back and compared, the ones in which any cluster listed a
	// light, and the ones that disagreed with the CPU or overflowed the index list.
	std::uint64_t frames{0};
	std::uint64_t litFrames{0};
	std::uint64_t failures{0};
	// The most entries of the index list any frame used, and the most lights any cluster listed: what
	// the list's budget (Renderer::kLightIndicesPerCluster) is measured against.
	std::uint32_t peakEntries{0};
	std::uint32_t peakClusterLights{0};
	// What the first failure was.
	std::string firstFailure;
};

// The grid is the assignment's offset and count per cluster, the indices the list's entries after
// its allocator, and `reserved` the allocator's final value.
ClusterCheck CheckClusterAssignment(
    const ClusterView &view, const glm::mat3 &viewMatrix, std::span<const GpuLight> lights,
    std::span<const glm::uvec2> grid, std::span<const std::uint32_t> indices, std::uint32_t reserved);
} // namespace Jettison::Render
