#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "material.glsl"

// Unlit: the surface is its own colour, at an intensity, whatever lights it or does not. For what
// is a light rather than lit by one - a beacon, a running light, a lit sign - and for what is
// shown rather than shaded. Its vertex stage is mesh.vert built with UNLIT.
layout(set = 1, binding = 0) uniform sampler2D colorTexture;

// rgb: colour, a: intensity in the HDR target's linear units, multiplied by the texture and the
// vertex colour.
const uint kColor = 0u;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
	vec4 color = MaterialParameter(kColor);
	vec4 texel = texture(colorTexture, fragTexCoord);
	outColor = vec4(texel.rgb * fragColor * color.rgb * color.a, texel.a);
}
