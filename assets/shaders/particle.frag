#version 450
#extension GL_GOOGLE_include_directive : require

// A particle's light and cover (docs/architecture.md, "Particles"): a disc, or a capsule along its
// streak, fading as (1 - r^2)^2 from its middle line to its edge. It adds its colour and, when its
// emitter has an albedo, the light it scatters, lit through the one lighting include as a small
// Lambertian sphere (ShadeVolume); and it lets through what it does not hide of what is behind it,
// the second output, which BlendMode::Transmissive multiplies what is there by.

#include "draw.glsl"
#include "frame.glsl"
#include "lighting.glsl"
#include "particles.glsl"

layout(std430, set = 0, binding = 14) readonly buffer EmitterList
{
	Emitter emitters[];
};

layout(location = 0) in vec2 fragLocal;
layout(location = 1) flat in vec2 fragExtent;
layout(location = 2) flat in vec4 fragColor;
layout(location = 3) in vec3 fragRenderPosition;

layout(location = 0, index = 0) out vec4 outColor;
layout(location = 0, index = 1) out vec4 outThrough;

void main()
{
	// The distance from its middle line, its streak from its head back.
	float along = fragLocal.x - clamp(fragLocal.x, -fragExtent.y, 0.0);
	// Past the edge it is nothing, which adds nothing and hides nothing; a discard would want the
	// device's demote-to-helper feature, for no saving a particle's few pixels would show.
	float r2 = (along * along + fragLocal.y * fragLocal.y) / (fragExtent.x * fragExtent.x);
	float edge = max(1.0 - r2, 0.0);
	float falloff = edge * edge;
	float cover = fragColor.a * falloff;
	vec3 light = fragColor.rgb * falloff;
	Emitter emitter = emitters[draw.materialOffset];
	if (emitter.albedo.a != 0.0)
		light += emitter.albedo.rgb * ShadeVolume(fragRenderPosition) * cover;
	outColor = vec4(light, cover);
	outThrough = vec4(vec3(1.0 - cover), 1.0 - cover);
}
