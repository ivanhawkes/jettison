#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame.glsl"
#include "outlines.glsl"
#include "post.glsl"

// The jump flood's seeds: each pixel of a targeted shape knows of itself, and every other pixel of no
// seed (post.glsl's kNoSeed). The IDs are the depth prepass's, at sample zero.
layout(set = 1, binding = 0) uniform sampler2DMS objectIds;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out uvec2 outSeed;

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	uint outline = ObjectOutline(texelFetch(objectIds, pixel, 0).r);
	bool seed = outline != 0u && outlines[outline - 1u].style.x == kOutlineTargeted;
	outSeed = seed ? uvec2(pixel) : uvec2(kNoSeed);
}
