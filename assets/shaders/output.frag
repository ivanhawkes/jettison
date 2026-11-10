#version 450
#extension GL_GOOGLE_include_directive : require

#include "colour.glsl"

// The resolved HDR target, linear light.
layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(push_constant) uniform Output
{
	float exposure;
	// Non-zero when the swapchain is UNORM and this pass has to do the sRGB encoding itself.
	uint encodeSrgb;
} outputSettings;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
	// Exposure and a clamp to the display's range: the tonemapping operator arrives in phase 5,
	// and until then values above one are clipped, not compressed.
	vec3 linear = clamp(texture(hdrColor, fragUv).rgb * outputSettings.exposure, 0.0, 1.0);
	outColor = vec4(outputSettings.encodeSrgb != 0u ? LinearToSrgb(linear) : linear, 1.0);
}
