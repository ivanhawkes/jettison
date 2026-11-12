#version 450
#extension GL_GOOGLE_include_directive : require

#include "post.glsl"

// A bloom level from the one above it (post.glsl's BloomDownsample): thirteen bilinear taps of the
// level above, at half and whole texels about this pixel's centre. The first level is taken from the
// frame and weighs its boxes by their brightness.
layout(set = 1, binding = 0) uniform sampler2D source;

layout(push_constant) uniform Post
{
	// xy: a texel of the level above, in its coordinates; z: 1 for the first level.
	vec4 parameters;
	vec4 unused;
} post;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

vec3 Tap(float x, float y)
{
	// Half-float's largest is 65504; the sun's disc is clamped well under it, and nothing brighter is
	// let in.
	return min(texture(source, fragUv + vec2(x, y) * post.parameters.xy).rgb, vec3(30000.0));
}

void main()
{
	vec3 colour = BloomDownsample(
	    Tap(-2.0, -2.0), Tap(0.0, -2.0), Tap(2.0, -2.0), Tap(-2.0, 0.0), Tap(0.0, 0.0), Tap(2.0, 0.0),
	    Tap(-2.0, 2.0), Tap(0.0, 2.0), Tap(2.0, 2.0), Tap(-1.0, -1.0), Tap(1.0, -1.0), Tap(-1.0, 1.0),
	    Tap(1.0, 1.0), post.parameters.z != 0.0);
	outColor = vec4(colour, 1.0);
}
