#version 450
#extension GL_GOOGLE_include_directive : require

#include "colour.glsl"
#include "tonemap.glsl"

// The resolved HDR target, linear light.
layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(push_constant) uniform Output
{
	// A colour over the scene and how much of it, from 0 to 1: a jump's effect.
	vec4 fade;
	float exposure;
	// Non-zero when the swapchain is UNORM and this pass has to do the sRGB encoding itself.
	uint encodeSrgb;
} outputSettings;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
	vec3 exposed = max(texture(hdrColor, fragUv).rgb * outputSettings.exposure, vec3(0.0));
	vec3 display = mix(
	    clamp(TonemapPbrNeutral(exposed), 0.0, 1.0), outputSettings.fade.rgb, outputSettings.fade.a);
	outColor = vec4(outputSettings.encodeSrgb != 0u ? LinearToSrgb(display) : display, 1.0);
}
