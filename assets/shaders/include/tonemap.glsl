// Tonemapping: the HDR target's linear light, after exposure, onto the display's 0 to 1.
//
// The operator is Khronos PBR Neutral (Khronos 3D Formats Working Group, 2024), from its reference
// implementation. Below its knee it is the identity, less a small offset that keeps blacks black,
// so a base colour lit by white light of intensity one shows at the colour it was authored; above
// it, highlights are compressed towards white and desaturated as they go, and nothing clips. Hue is
// kept, which a filmic curve applied per channel does not do.
//
// Written in the GLSL that GLM also compiles as C++, as brdf.glsl is, so the render suite tests
// this source (BrdfTests.cpp).

#ifndef JETTISON_TONEMAP_GLSL
#define JETTISON_TONEMAP_GLSL

vec3 TonemapPbrNeutral(vec3 color)
{
	const float startCompression = 0.8f - 0.04f;
	const float desaturation = 0.15f;

	float lowest = min(color.r, min(color.g, color.b));
	float offset = lowest < 0.08f ? lowest - 6.25f * lowest * lowest : 0.04f;
	color -= offset;

	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression)
		return color;

	const float knee = 1.0f - startCompression;
	float newPeak = 1.0f - knee * knee / (peak + knee - startCompression);
	color *= newPeak / peak;

	float towardsWhite = 1.0f - 1.0f / (desaturation * (peak - newPeak) + 1.0f);
	return mix(color, vec3(newPeak), towardsWhite);
}

#endif
