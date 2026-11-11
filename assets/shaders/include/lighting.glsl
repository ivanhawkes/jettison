// The one lighting implementation (docs/architecture.md, "Rules that follow"): every lit program
// shades a surface through ShadeSurface, which adds the sun, the local lights of the cluster the
// fragment falls in and the environment, all through brdf.glsl. Phase 5d shadows the sun.
//
// Fragment stages only, since it finds the cluster from gl_FragCoord. Needs frame.glsl.

#ifndef JETTISON_LIGHTING_GLSL
#define JETTISON_LIGHTING_GLSL

#include "brdf.glsl"
#include "clusters.glsl"

// What ShadeSurface shows, by frame.counts.y: the lit surface, or the lights-per-cluster heat map.
const uint kDebugViewShaded = 0u;
const uint kDebugViewClusterHeat = 1u;

// Each cluster's lights, as cluster_lights.comp wrote them: x the offset of its first entry in the
// index list, y how many it has.
layout(std430, set = 0, binding = 8) readonly buffer ClusterGrid
{
	uvec2 clusterLights[];
};

// The light list's indices, cluster by cluster. The count of entries reserved comes first: the
// assignment program's allocator, which no fragment reads.
layout(std430, set = 0, binding = 9) readonly buffer LightIndexList
{
	uint lightIndexCount;
	uint lightIndices[];
};

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

// The cluster a fragment falls in, or kClusterCount past the cluster range.
uint FragmentCluster(vec3 renderPosition)
{
	float depth = -(frame.view * vec4(renderPosition, 1.0)).z;
	return ClusterAt(
	    gl_FragCoord.xy, depth, frame.clusterTile.xy, frame.clusterDepth.x, frame.clusterDepth.y);
}

// One local light's share: inverse square inside its range, windowed to zero at it, and for a spot
// shaped by its cone.
vec3 LocalLighting(Surface surface, vec3 normal, vec3 toViewer, vec3 renderPosition, Light light)
{
	vec3 toLight = light.positionRange.xyz - renderPosition;
	float distanceSquared = dot(toLight, toLight);
	vec3 lightDirection = toLight * inversesqrt(max(distanceSquared, 1e-12));
	float attenuation = RangeAttenuation(distanceSquared, light.positionRange.w) *
	                    SpotAttenuation(light.directionType.xyz, -lightDirection, light.cone.x,
	                                    light.cone.y);
	return EvaluateBrdf(surface, normal, toViewer, lightDirection) * light.colorIntensity.rgb *
	       (light.colorIntensity.a * attenuation);
}

// The heat map's colour for a cluster's light count: dark blue for none, through cyan, green and
// yellow to red at kHeatMapFull, and magenta past it. Grey is past the cluster range, where no
// local light is assigned.
const uint kHeatMapFull = 16u;

vec3 ClusterHeat(uint cluster)
{
	if (cluster >= kClusterCount)
		return vec3(0.12);
	uint count = clusterLights[cluster].y;
	if (count == 0u)
		return vec3(0.01, 0.01, 0.08);
	if (count > kHeatMapFull)
		return vec3(1.0, 0.0, 1.0);
	float heat = float(count) / float(kHeatMapFull);
	vec3 cold = vec3(0.0, 0.2, 1.0);
	vec3 cool = vec3(0.0, 0.9, 0.9);
	vec3 warm = vec3(0.1, 0.9, 0.1);
	vec3 hot = vec3(1.0, 0.9, 0.0);
	vec3 full = vec3(1.0, 0.05, 0.0);
	if (heat < 0.25)
		return mix(cold, cool, heat * 4.0);
	if (heat < 0.5)
		return mix(cool, warm, heat * 4.0 - 1.0);
	if (heat < 0.75)
		return mix(warm, hot, heat * 4.0 - 2.0);
	return mix(hot, full, heat * 4.0 - 3.0);
}

// The light a surface sends towards the camera, which is at the render-space origin.
vec3 ShadeSurface(Surface surface, vec3 normal, vec3 renderPosition)
{
	vec3 toViewer = normalize(-renderPosition);
	// An interpolated normal can lean away from a viewer that can still see its triangle; it is
	// bent back to the silhouette rather than shaded black.
	normal = normalize(normal - toViewer * min(dot(normal, toViewer) - 0.001, 0.0));

	uint cluster = FragmentCluster(renderPosition);
	// Flat colour, darkened as the surface turns away so the shapes still read. The material's
	// emission is still added over it, so a lit window shows through.
	if (frame.counts.y == kDebugViewClusterHeat)
		return ClusterHeat(cluster) * (0.5 + 0.5 * max(dot(normal, toViewer), 0.0));

	vec3 radiance = vec3(0.0);
	// The sun is one direction for the whole frame: a star 150 million kilometres off subtends half
	// a degree, and its light does not fall off across a station.
	if (frame.sunDirection.w != 0.0)
		radiance += EvaluateBrdf(surface, normal, toViewer, frame.sunDirection.xyz) *
		            frame.sunColorIntensity.rgb * frame.sunColorIntensity.a;
	// The cluster's lights: those whose range reaches it, and no others. Past the cluster range
	// there are none, and the surface has the sun, the sky and its own emission.
	if (cluster < kClusterCount)
	{
		uvec2 entry = clusterLights[cluster];
		for (uint index = 0u; index < entry.y; ++index)
			radiance += LocalLighting(
			    surface, normal, toViewer, renderPosition, lights[lightIndices[entry.x + index]]);
	}
	return radiance + EnvironmentLighting(surface, normal, toViewer);
}

#endif
