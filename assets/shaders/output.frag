#version 450
#extension GL_GOOGLE_include_directive : require

#include "colour.glsl"
#include "post.glsl"
#include "tonemap.glsl"

// The resolved HDR target, linear light; the bloom chain's top level, which holds every level's
// light (post.glsl's ApplyBloom); and the stencil, where post-processing leaves a pixel alone.
layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D bloom;
layout(set = 1, binding = 2) uniform usampler2D stencil;

layout(push_constant) uniform Output
{
	// A colour over the scene and how much of it, from 0 to 1: a jump's effect.
	vec4 fade;
	// x: the exposure, y: the bloom's strength, 0 for none; z: 1 when the swapchain is UNORM and this
	// pass has to do the sRGB encoding itself.
	vec4 settings;
} outputSettings;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

const uint kStencilUnprocessed = 4u;

void main()
{
	vec3 scene = texture(hdrColor, fragUv).rgb;
	float strength = outputSettings.settings.y;
	if (strength > 0.0 &&
	    (texelFetch(stencil, ivec2(gl_FragCoord.xy), 0).r & kStencilUnprocessed) == 0u)
		scene = ApplyBloom(scene, texture(bloom, fragUv).rgb, strength);
	vec3 exposed = max(scene * outputSettings.settings.x, vec3(0.0));
	vec3 display = mix(
	    clamp(TonemapPbrNeutral(exposed), 0.0, 1.0), outputSettings.fade.rgb, outputSettings.fade.a);
	outColor = vec4(outputSettings.settings.z != 0.0 ? LinearToSrgb(display) : display, 1.0);
}
