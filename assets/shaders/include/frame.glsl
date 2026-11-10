// The frame: descriptor set 0, written once per frame in flight and bound for every scene program.
// The C++ side is Render::FrameUBO and Render::GpuLight (render/vulkan/FrameData.h), whose sizes
// and offsets are asserted there against this layout. Positions are in render space: metres from
// the camera, which is at the origin.

#ifndef JETTISON_FRAME_GLSL
#define JETTISON_FRAME_GLSL

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
	// x: the lights in the light list.
	uvec4 counts;
} frame;

// The skin palette: every skinned draw's joint matrices, one after another. A draw finds its own
// from the offset in its push constants.
layout(std430, set = 0, binding = 1) readonly buffer JointPalette
{
	mat4 jointMatrices[];
};

// The frame's lights, collected on the CPU in render space. Phase 5c assigns them to clusters;
// until then every fragment loops over all of them.
struct Light
{
	vec4 position;
	vec4 colorIntensity;
};

layout(std430, set = 0, binding = 2) readonly buffer LightList
{
	Light lights[];
};

// Binding 3 is the frame's material parameters, which material.glsl declares for the programs that
// read them, and bindings 4 to 6 are the environment, which lighting.glsl declares.

#endif
