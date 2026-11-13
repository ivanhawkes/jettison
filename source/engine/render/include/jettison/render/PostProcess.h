#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Jettison::Scene
{
struct MaskComponent;
}

namespace Jettison::Render
{
// The post chain's CPU half (docs/architecture.md, "Post-processing", "Masks" and "Outlines"): the
// stencil bits a draw lays down, an object's ID, the outlines a frame draws, and the bloom levels'
// sizes. Nothing here touches the device. The passes' arithmetic is post.glsl, which the render suite
// runs as C++ through the functions at the end.

// The depth target's stencil, a byte a pixel, written by every opaque draw. Bits 0 to 2 are the
// engine's, 3 to 7 the game's (Scene::MaskComponent).
inline constexpr std::uint8_t kStencilCockpit = 1u << 0;
inline constexpr std::uint8_t kStencilOutlined = 1u << 1;
inline constexpr std::uint8_t kStencilUnprocessed = 1u << 2;
inline constexpr std::uint8_t kStencilEngineBits = 0x07;
inline constexpr std::uint32_t kStencilGameShift = 3;

// A draw's stencil byte, from the mask over it (null for none) and whether it is outlined.
std::uint8_t StencilBits(const Scene::MaskComponent *mask, bool outlined);

// The most outlines a frame draws, and the most draws whose IDs it tells apart; a draw past it has
// ID 0, as the sky does.
inline constexpr std::uint32_t kMaxOutlines = 255;
inline constexpr std::uint32_t kMaxObjectIds = 65535;

// A draw's object ID as its instance carries it and the depth prepass writes it (post.glsl's
// ObjectOutline): its index plus one in the low sixteen bits, and its outline's index plus one in the
// eight above.
std::uint32_t ObjectId(std::uint32_t draw, std::uint32_t outline);

// One entry of the frame's outlines, std430: post.frag's Outline.
struct GpuOutline
{
	// rgb: linear colour, a: intensity.
	glm::vec4 color{0.0f};
	// x: 0 for the selected's line, 1 for the targeted's glow; y: the glow's width in pixels.
	glm::vec4 style{0.0f};
};
static_assert(sizeof(GpuOutline) == 32);

// The bloom chain's levels below the frame, and the jump flood's steps, as post.glsl has them.
inline constexpr std::uint32_t kBloomLevels = 5;
inline constexpr std::uint32_t kFloodSteps = 5;

// The bloom chain's levels below a frame of this size, each half the one above, rounded down and
// never under a pixel.
std::vector<glm::uvec2> BloomExtents(glm::uvec2 frame);

// post.glsl, as the passes run it.
namespace Post
{
int AoSamples();
glm::vec3 ViewPositionAt(
    glm::vec2 pixel, float depth, glm::vec2 viewport, glm::vec2 projectionScale, float nearPlane);
float ScreenRadius(float radius, float viewDepth, float viewportHeight, float verticalScale);
glm::vec3 AoSpiralSample(int index, float angle);
float AoPixelAngle(int x, int y);
float ObscuranceSample(glm::vec3 p, glm::vec3 n, glm::vec3 q, float radius);
float AmbientAccess(float sum, float samples, float radius, float intensity);
float SpecularAccess(float nDotV, float access, float roughness);
float AoBlurWeight(float depth, float neighbourDepth);
int BloomLevels();
// The thirteen taps, a to m, as post.glsl lays them out.
glm::vec3 BloomDownsample(const glm::vec3 (&taps)[13], bool karis);
glm::vec3 BloomUpsample(const glm::vec3 (&taps)[9]);
glm::vec3 ApplyBloom(glm::vec3 frame, glm::vec3 bloomTop, float strength);
std::uint32_t NoSeed();
int FloodSteps();
int FloodStride(int step);
glm::uvec2 NearerSeed(glm::uvec2 pixel, glm::uvec2 best, glm::uvec2 candidate);
float GlowFalloff(float distance, float width);
std::uint32_t ObjectOutline(float objectId);
glm::vec3 LutCoordinate(glm::vec3 encoded, float size);
} // namespace Post
} // namespace Jettison::Render
