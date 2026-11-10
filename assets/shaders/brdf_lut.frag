#version 450
#extension GL_GOOGLE_include_directive : require

#include "brdf.glsl"

// The split sum's lookup table: by n.v across and perceptual roughness down, the specular lobe's
// scale and bias on F0 (IntegrateSpecularBrdf). The texel centres are the values integrated at, so
// lighting.glsl samples it at (n.v, roughness) directly.

layout(push_constant) uniform Lut
{
	uint sampleCount;
} lut;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec2 outScaleBias;

void main()
{
	outScaleBias = IntegrateSpecularBrdf(fragUv.x, fragUv.y, lut.sampleCount);
}
