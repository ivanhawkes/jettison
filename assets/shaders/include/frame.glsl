// The frame: descriptor set 0, written once per frame in flight and bound for every scene program.
// The compute programs that build the cluster grid bind it too. The C++ side is Render::FrameUBO and
// Render::GpuLight (render/vulkan/FrameData.h), whose sizes and offsets are asserted there against
// this layout. Positions are in render space: metres from the camera, which is at the origin, along
// the system frame's axes; view space is render space turned by the view matrix, which is a
// rotation only.

#ifndef JETTISON_FRAME_GLSL
#define JETTISON_FRAME_GLSL

#include "body.glsl"
#include "sky.glsl"

layout(set = 0, binding = 0) uniform FrameConstants
{
	mat4 view;
	mat4 projection;
	// The system's directional light: xyz towards it, w 1 when there is one.
	vec4 sunDirection;
	vec4 sunColorIntensity;
	// The sun as the sky draws it: x the cosine of its angular radius, y its angular radius in
	// radians, z its disc's radiance per unit of sunColorIntensity's colour.
	vec4 sunDisc;
	// x: the environment's roughest specular level, which roughness 1 samples; y: the angle one
	// pixel covers at the centre of the view, in radians.
	vec4 environment;
	Sky sky;
	// x: the lights in the light list, y: the debug view (kDebugView*, lighting.glsl), z: 1 when the
	// sun casts shadows this frame, w: the bodies in the body list.
	uvec4 counts;
	// The cluster grid (clusters.glsl): x the near plane and y the cluster range, both view depths
	// in metres.
	vec4 clusterDepth;
	// xy: a cluster tile's size in pixels, zw: the viewport's size in pixels.
	vec4 clusterTile;
	// The sun's shadow cascades (shadows.glsl): each one's matrix from render space to its map, the
	// view depth each ends at, and a texel's size in metres in each.
	mat4 shadowMatrices[4];
	vec4 shadowSplits;
	vec4 shadowTexels;
} frame;

// The skin palette: every skinned draw's joint matrices, one after another. A draw finds its own
// from the offset in its push constants.
layout(std430, set = 0, binding = 1) readonly buffer JointPalette
{
	mat4 jointMatrices[];
};

// The frame's local lights, collected on the CPU in render space; the cluster passes assign them to
// the grid, and a fragment loops over its cluster's share of them (lighting.glsl).
struct Light
{
	// xyz: the position, w: the range in metres, past which the light reaches nothing.
	vec4 positionRange;
	// rgb: linear colour, a: intensity, the irradiance at one metre.
	vec4 colorIntensity;
	// xyz: the way a spot points, unit length, w: kLightPoint or kLightSpot (clusters.glsl).
	vec4 directionType;
	// x, y: the spot's angular falloff as a scale and offset on the cosine (SpotAttenuation), 0 and 1
	// for a point light; z, w: the outer cone angle's cosine and sine, for culling.
	vec4 cone;
};

layout(std430, set = 0, binding = 2) readonly buffer LightList
{
	Light lights[];
};

// The celestial bodies drawn this frame with an atmosphere or rings (body.glsl): the planet program
// reads its own by the index its instance carries, and the atmosphere program reads them all.
layout(std430, set = 0, binding = 12) readonly buffer BodyList
{
	Body bodies[];
};

// Binding 3 is the frame's material parameters, which material.glsl declares for the programs that
// read them, and bindings 4 to 6 are the environment, which lighting.glsl declares. Bindings 7 to 9
// are the cluster grid, one set shared by the frames in flight as the render targets are: 7 the
// clusters' bounds, which only the compute programs read, and 8 and 9 each cluster's offset and
// count into the light index list, and the list, which the assignment program writes and
// lighting.glsl reads. Binding 10 is the sun's shadow maps, which lighting.glsl declares, shared as
// the grid is. Binding 11 is the frame's instances, which mesh_vertex.glsl declares.

#endif
