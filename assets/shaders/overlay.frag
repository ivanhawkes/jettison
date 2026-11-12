#version 450
#extension GL_GOOGLE_include_directive : require

#include "colour.glsl"
#include "overlay.glsl"

// sRGB-encoded with alpha premultiplied, as the vertex colours are; white for an untextured draw.
layout(set = 0, binding = 0) uniform sampler2D overlayTexture;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main()
{
	// Multiplied and filtered as encoded, as a browser does; blended premultiplied ("over").
	vec4 color = fragColor * texture(overlayTexture, fragUv);
	// An sRGB attachment encodes what is written to it, so the colour goes to it in linear light:
	// alpha off, decoded, alpha on again. The blend then mixes in linear light, where a browser mixes
	// encoded values, which leaves a half-covered edge of light text on dark a little brighter.
	if (overlayDraw.decodeSrgb != 0u && color.a > 0.0)
		color.rgb = SrgbToLinear(color.rgb / color.a) * color.a;
	outColor = color;
}
