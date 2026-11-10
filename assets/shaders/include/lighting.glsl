// The one lighting implementation (docs/architecture.md, "Rules that follow"): every lit program
// shades a surface through ShadeSurface, which adds the sun, the frame's local lights and the
// environment, all through brdf.glsl. Phase 5c replaces the loop over every local light with the
// cluster's list; phase 5d shadows the sun.
//
// Needs frame.glsl.

#ifndef JETTISON_LIGHTING_GLSL
#define JETTISON_LIGHTING_GLSL

#include "brdf.glsl"

// The star system's environment (Render::Environment): the sky's radiance prefiltered for GGX at
// one roughness per level, its cosine-weighted irradiance over pi, and the split sum's scale and
// bias by n.v and roughness.
layout(set = 0, binding = 4) uniform samplerCube environmentSpecular;
layout(set = 0, binding = 5) uniform samplerCube environmentIrradiance;
layout(set = 0, binding = 6) uniform sampler2D brdfLut;

// The environment's light on a surface, by the split sum: the specular lobe's share is the
// prefiltered radiance along the reflection, scaled by the lobe's reflectance under white light.
vec3 EnvironmentLighting(Surface surface, vec3 normal, vec3 toViewer)
{
	float nDotV = clamp(dot(normal, toViewer), 0.0001, 1.0);
	vec3 reflected = reflect(-toViewer, normal);
	vec2 scaleBias = texture(brdfLut, vec2(nDotV, surface.roughness)).rg;
	vec3 prefiltered =
	    textureLod(environmentSpecular, reflected, surface.roughness * frame.environment.x).rgb;
	vec3 specular = prefiltered * (SpecularColor(surface) * scaleBias.x + scaleBias.y);
	vec3 diffuse = texture(environmentIrradiance, normal).rgb * DiffuseColor(surface);
	return diffuse + specular;
}

// The light a surface sends towards the camera, which is at the render-space origin.
vec3 ShadeSurface(Surface surface, vec3 normal, vec3 renderPosition)
{
	vec3 toViewer = normalize(-renderPosition);
	// An interpolated normal can lean away from a viewer that can still see its triangle; it is
	// bent back to the silhouette rather than shaded black.
	normal = normalize(normal - toViewer * min(dot(normal, toViewer) - 0.001, 0.0));

	vec3 radiance = vec3(0.0);
	// The sun is one direction for the whole frame: a star 150 million kilometres off subtends half
	// a degree, and its light does not fall off across a station.
	if (frame.sunDirection.w != 0.0)
		radiance += EvaluateBrdf(surface, normal, toViewer, frame.sunDirection.xyz) *
		            frame.sunColorIntensity.rgb * frame.sunColorIntensity.a;
	// The stand-in for the clustered lights: every light reaches every fragment, with an unbounded
	// falloff, until phase 5c gives lights a range and a cluster.
	for (uint index = 0u; index < frame.counts.x; ++index)
	{
		Light light = lights[index];
		vec3 toLight = light.position.xyz - renderPosition;
		float distanceToLight = length(toLight);
		vec3 lightDirection = toLight / max(distanceToLight, 0.0001);
		float attenuation =
		    1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
		radiance += EvaluateBrdf(surface, normal, toViewer, lightDirection) *
		            light.colorIntensity.rgb * (light.colorIntensity.a * attenuation);
	}
	return radiance + EnvironmentLighting(surface, normal, toViewer);
}

#endif
