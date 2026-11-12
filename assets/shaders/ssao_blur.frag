#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame.glsl"
#include "post.glsl"

// The ambient access blurred over the 4 x 4 that its spiral's turns repeat over, which takes the
// pattern out, each neighbour weighed by how near its depth is to the pixel's so the occlusion keeps
// to its own surface (post.glsl's AoBlurWeight).
layout(set = 1, binding = 0) uniform sampler2D rawAccess;
layout(set = 1, binding = 1) uniform sampler2DMS sceneDepth;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out float outAccess;

float ViewDepthAt(ivec2 pixel)
{
	return frame.clusterDepth.x / max(texelFetch(sceneDepth, pixel, 0).r, 1e-30);
}

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	float centre = texelFetch(sceneDepth, pixel, 0).r;
	if (centre <= 0.0)
	{
		outAccess = 1.0;
		return;
	}
	float depth = frame.clusterDepth.x / centre;
	ivec2 last = ivec2(frame.clusterTile.zw) - 1;
	float sum = 0.0;
	float weights = 0.0;
	for (int y = -2; y < 2; ++y)
		for (int x = -2; x < 2; ++x)
		{
			ivec2 at = clamp(pixel + ivec2(x, y), ivec2(0), last);
			float weight = AoBlurWeight(depth, ViewDepthAt(at));
			sum += texelFetch(rawAccess, at, 0).r * weight;
			weights += weight;
		}
	outAccess = weights > 0.0 ? sum / weights : texelFetch(rawAccess, pixel, 0).r;
}
