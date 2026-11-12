#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "frame.glsl"
#include "lighting.glsl"
#include "material.glsl"

// Glass (docs/architecture.md, "Transparency"): a thin dielectric pane, a canopy or a window, drawn
// in the transparent pass. Its surface reflects as any dielectric does, through the one lighting
// include, with no diffuse lobe: the sun's highlight, the clusters' lights and the environment. What
// it does not reflect it lets through, tinted, as the second output, which BlendMode::Transmissive
// multiplies what is behind it by, by colour; nothing bends on its way through. Both faces are
// drawn, the back one with its normal turned to the camera, so a canopy is two panes from outside and
// one from inside, and since each pane's light is added and what is behind multiplied, the order of
// the two changes only how much of the far one's reflection the near one tints. Its vertex stage is
// mesh.vert, built as glass.vert.
layout(set = 1, binding = 0) uniform sampler2D tintTexture;

// rgb: what it lets through at normal incidence, by colour, multiplied by the texture.
const uint kTint = 0u;
// x: perceptual roughness, frosted the rougher.
const uint kSurface = 1u;
// rgb: colour, a: intensity: light from within it, a lit display or a frame's glow.
const uint kEmissive = 2u;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragRenderPosition;

layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outThrough;

void main()
{
	vec3 normal = normalize(fragNormal);
	if (!gl_FrontFacing)
		normal = -normal;
	Surface surface =
	    Surface(vec3(0.0), 0.0, clamp(MaterialParameter(kSurface).x, 0.0, 1.0));
	vec3 reflected = ShadeSurface(surface, normal, fragRenderPosition);
	vec3 reflectance = SpecularReflectance(surface, normal, fragRenderPosition);
	vec3 tint = MaterialParameter(kTint).rgb * texture(tintTexture, fragTexCoord).rgb;
	vec4 emissive = MaterialParameter(kEmissive);
	outColor = vec4(reflected + emissive.rgb * emissive.a, 0.0);
	outThrough = vec4(tint * (vec3(1.0) - reflectance), 1.0);
}
