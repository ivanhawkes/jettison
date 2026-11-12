#version 450
#extension GL_GOOGLE_include_directive : require

#include "post.glsl"

// A bloom level added to from the one below it (post.glsl's BloomUpsample): a tent over nine bilinear
// taps of the level below, a texel apart, blended onto what the way down left here.
layout(set = 1, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Post
{
	// xy: a texel of the level below, in its coordinates.
	vec4 parameters;
	vec4 unused;
} post;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

vec3 Tap(float x, float y)
{
	return texture(source, fragUv + vec2(x, y) * post.parameters.xy).rgb;
}

void main()
{
	vec3 colour = BloomUpsample(
	    Tap(-1.0, -1.0), Tap(0.0, -1.0), Tap(1.0, -1.0), Tap(-1.0, 0.0), Tap(0.0, 0.0), Tap(1.0, 0.0),
	    Tap(-1.0, 1.0), Tap(0.0, 1.0), Tap(1.0, 1.0));
	outColor = vec4(colour, 1.0);
}
