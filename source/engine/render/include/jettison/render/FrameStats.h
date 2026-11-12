#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <jettison/render/Shadows.h>

namespace Jettison::Render
{
// How long the GPU spent in one declared pass, in milliseconds: the last frame measured, and the
// mean since the averages were last reset.
struct PassTiming
{
	std::string name;
	double lastMs{0.0};
	double meanMs{0.0};
};

// What the frame drew and what it left out (docs/architecture.md, "The frame"), counted as it was
// prepared: the meshes, the instanced draws they became, and the triangles those submit.
struct FrameStats
{
	std::uint32_t meshes{0};
	// Left out of the view: outside the frustum, behind a planet's horizon, or too small to see.
	std::uint32_t frustumCulled{0};
	std::uint32_t horizonCulled{0};
	std::uint32_t detailCulled{0};
	std::uint32_t viewBatches{0};
	std::uint64_t viewTriangles{0};
	// Each cascade's casters, and the instanced draws they became.
	std::array<std::uint32_t, kShadowCascades> shadowCasters{};
	std::array<std::uint32_t, kShadowCascades> shadowBatches{};
	std::uint64_t shadowTriangles{0};
	// The active lights, and those the light list kept.
	std::uint32_t lights{0};
	std::uint32_t listedLights{0};
};
} // namespace Jettison::Render
