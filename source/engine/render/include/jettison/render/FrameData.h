#pragma once

#include <glm/glm.hpp>

#include <array>
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
	// x: the lights in the light list, y: the debug view (Render::DebugView), z: 1 when the sun
	// casts shadows this frame, w: the bodies in the body list.
	alignas(16) glm::uvec4 counts{0u};
	// The cluster grid (clusters.glsl): x the near plane, y the cluster range, as view depths.
	alignas(16) glm::vec4 clusterDepth{0.0f};
	// xy: a cluster tile's size in pixels, zw: the viewport's size in pixels.
	alignas(16) glm::vec4 clusterTile{0.0f};
	// The sun's shadow cascades (render/Shadows.h): render space to each map, the view depth each ends
	// at, and a texel's size in metres in each.
	alignas(16) std::array<glm::mat4, 4> shadowMatrices{};
	alignas(16) glm::vec4 shadowSplits{0.0f};
	alignas(16) glm::vec4 shadowTexels{0.0f};
	// x: 1 when the opaque surfaces read their ambient access, y: the outlines in the outline list,
	// z: 1 when there is a glow among them.
	alignas(16) glm::uvec4 effects{0u};
};
static_assert(sizeof(FrameUBO) == 624);
static_assert(offsetof(FrameUBO, projection) == 64);
static_assert(offsetof(FrameUBO, sunDirection) == 128);
static_assert(offsetof(FrameUBO, sunColorIntensity) == 144);
static_assert(offsetof(FrameUBO, sunDisc) == 160);
static_assert(offsetof(FrameUBO, environment) == 176);
static_assert(offsetof(FrameUBO, sky) == 192);
static_assert(offsetof(FrameUBO, counts) == 272);
static_assert(offsetof(FrameUBO, clusterDepth) == 288);
static_assert(offsetof(FrameUBO, clusterTile) == 304);
static_assert(offsetof(FrameUBO, shadowMatrices) == 320);
static_assert(offsetof(FrameUBO, shadowSplits) == 576);
static_assert(offsetof(FrameUBO, shadowTexels) == 592);
static_assert(offsetof(FrameUBO, effects) == 608);

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

// One entry of the frame's atmospheres, std430: atmosphere.glsl's Atmosphere. Render::PackAtmosphere
// (render/Atmosphere.h) fills it from an AtmosphereComponent.
struct GpuAtmosphere
{
	// xyz: the body's centre in render space, w: its surface's radius.
	glm::vec4 centreRadius{0.0f};
	// rgb: Rayleigh scattering per metre at the surface, a: its scale height.
	glm::vec4 rayleigh{0.0f};
	// x: Mie scattering, y: its extinction, z: its scale height, w: its anisotropy.
	glm::vec4 mie{0.0f};
	// x: the top's radius, y: 1 when there is an atmosphere.
	glm::vec4 shell{0.0f};
};
static_assert(sizeof(GpuAtmosphere) == 64);

// rings.glsl's Rings, std430. Render::PackRings (render/Atmosphere.h) fills it from a RingsComponent.
struct GpuRings
{
	// xyz: the plane's normal in render space, w: the inner radius.
	glm::vec4 normalInner{0.0f};
	// rgb: the albedo, a: the outer radius.
	glm::vec4 colorOuter{0.0f};
	// x: the densest band's opacity, y: the seed, z: 1 when there are rings.
	glm::vec4 shape{0.0f};
};
static_assert(sizeof(GpuRings) == 48);

// One entry of the frame's bodies, std430: frame.glsl's Body, a celestial body drawn with an
// atmosphere or rings. The atmosphere's centre and radius are the body's whether or not it has air.
struct GpuBody
{
	GpuAtmosphere atmosphere{};
	GpuRings rings{};
};
static_assert(sizeof(GpuBody) == 112);

// One entry of the frame's instance buffer, std430: mesh_vertex.glsl's Instance. Every draw is an
// instanced one (render/DrawBatches.h), and reads its transform here by gl_InstanceIndex.
struct GpuInstance
{
	// Object space to render space.
	glm::mat4 model{1.0f};
	// x: where the instance's joint matrices start in the skin palette, y: 1 when it is skinned, z:
	// one more than the index of the body in the frame's list of them, 0 for none, w: its object ID
	// (render/PostProcess.h).
	glm::uvec4 skin{0u};
};
static_assert(sizeof(GpuInstance) == 80);

// A slot of the particle pool, std430: particles.glsl's Particle. Device-local, shared by the frames
// in flight, and stepped in place once a frame (docs/architecture.md, "Particles").
struct GpuParticle
{
	// xyz: where it is in its emitter's frame, w: its age in seconds.
	glm::vec4 positionAge{0.0f};
	// xyz: its velocity in the frame, w: its lifetime, 0 for a slot holding none.
	glm::vec4 velocityLifetime{0.0f};
};
static_assert(sizeof(GpuParticle) == 32);

// One emitter this frame, std430: particles.glsl's Emitter. Render::ParticlePool (render/Particles.h)
// fills it.
struct GpuEmitter
{
	// The particles' frame to render space: a turn and a move, without the frame's scale.
	glm::mat4 frameToRender{1.0f};
	// xyz: the emitter now, in the frame; w: the radius particles are born within.
	glm::vec4 origin{0.0f};
	// xyz: the emitter at the last frame; w: the seconds since it.
	glm::vec4 previous{0.0f};
	// xyz: the way particles are sent, in the frame; w: the cosine of the cone's half-angle.
	glm::vec4 axis{0.0f, 0.0f, -1.0f, 1.0f};
	// x: speed, y: its jitter, z: drag a second, w: lifetime.
	glm::vec4 motion{0.0f};
	// xyz: the emitter's velocity in the frame, w: the lifetime's jitter.
	glm::vec4 inherited{0.0f};
	// x, y: the diameter at birth and death, z: the stretch in seconds, w: the opacity.
	glm::vec4 shape{0.0f};
	glm::vec4 colorStart{0.0f};
	glm::vec4 colorEnd{0.0f};
	// rgb: the albedo, a: 1 when it is lit.
	glm::vec4 albedo{0.0f};
	// x: the first slot in the pool, y: the slots, z: the slot of the first birth this frame, as an
	// index into its own, w: the births this frame.
	glm::uvec4 slots{0u};
	// x: the first birth's serial number, y: the seed, z: 1 when the slots are to be emptied first.
	glm::uvec4 spawn{0u};
};
static_assert(sizeof(GpuEmitter) == 240);
static_assert(offsetof(GpuEmitter, origin) == 64);
static_assert(offsetof(GpuEmitter, slots) == 208);
} // namespace Jettison::Render
