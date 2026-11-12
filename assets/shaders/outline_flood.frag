#version 450
#extension GL_GOOGLE_include_directive : require

#include "post.glsl"

// One step of the jump flood (post.glsl): the nearest seed known to this pixel or to any of the eight
// a stride away, in a fixed order.
layout(set = 1, binding = 0) uniform usampler2D previous;

layout(push_constant) uniform Post
{
	// x: the stride in pixels.
	vec4 parameters;
	vec4 unused;
} post;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out uvec2 outSeed;

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 last = textureSize(previous, 0) - 1;
	int stride = int(post.parameters.x);
	uvec2 best = uvec2(kNoSeed);
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
		{
			ivec2 at = pixel + ivec2(x, y) * stride;
			if (any(lessThan(at, ivec2(0))) || any(greaterThan(at, last)))
				continue;
			best = NearerSeed(uvec2(pixel), best, texelFetch(previous, at, 0).rg);
		}
	outSeed = best;
}
