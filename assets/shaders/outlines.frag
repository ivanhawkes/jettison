#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame.glsl"
#include "outlines.glsl"
#include "post.glsl"

// The outlines over the frame (docs/architecture.md, "Outlines"), tested against the stencil so that
// the hardware skips every pixel of an outlined shape, of the cockpit and of what post-processing
// leaves alone before it is shaded. A pixel beside an outlined shape's, by the stencil, is on the
// selected's line, in the colour of the outline its neighbour's ID names; and a pixel within a
// targeted shape's glow, by the jump flood, takes the glow's light, fading over its width.
layout(set = 1, binding = 0) uniform usampler2D nearestSeed;
layout(set = 1, binding = 1) uniform sampler2DMS objectIds;
layout(set = 1, binding = 2) uniform usampler2D stencil;

layout(location = 0) in vec2 fragUv;
// Premultiplied: the line covers what is there, and the glow adds to it.
layout(location = 0) out vec4 outColor;

const uint kStencilOutlined = 2u;

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 last = ivec2(frame.clusterTile.zw) - 1;
	const ivec2 neighbours[4] = ivec2[4](ivec2(1, 0), ivec2(-1, 0), ivec2(0, 1), ivec2(0, -1));
	for (int index = 0; index < 4; ++index)
	{
		ivec2 at = clamp(pixel + neighbours[index], ivec2(0), last);
		if ((texelFetch(stencil, at, 0).r & kStencilOutlined) == 0u)
			continue;
		uint outline = ObjectOutline(texelFetch(objectIds, at, 0).r);
		if (outline != 0u && outlines[outline - 1u].style.x == kOutlineSelected)
		{
			Outline line = outlines[outline - 1u];
			outColor = vec4(line.color.rgb * line.color.a, 1.0);
			return;
		}
	}
	// The flood runs only in a frame with a glow; otherwise its targets hold nothing of this frame's.
	vec4 glow = vec4(0.0);
	uvec2 seed = frame.effects.z != 0u ? texelFetch(nearestSeed, pixel, 0).rg : uvec2(kNoSeed);
	if (seed.x != kNoSeed)
	{
		uint outline = ObjectOutline(texelFetch(objectIds, ivec2(seed), 0).r);
		if (outline != 0u)
		{
			Outline shape = outlines[outline - 1u];
			float strength = GlowFalloff(distance(vec2(seed), vec2(pixel)), shape.style.y);
			glow = vec4(shape.color.rgb * (shape.color.a * strength), 0.0);
		}
	}
	// Nothing, premultiplied, where there is no glow: the blend leaves the frame as it is.
	outColor = glow;
}
