// The one lighting implementation (docs/architecture.md, "Rules that follow"): every lit program
// shades a surface through ShadeSurface, which adds the sun, the local lights of the cluster the
// fragment falls in and the environment, all through brdf.glsl. The sun is shadowed through its
// cascades (shadows.glsl).
//
// Fragment stages only, since it finds the cluster from gl_FragCoord. Needs frame.glsl.

#ifndef JETTISON_LIGHTING_GLSL
#define JETTISON_LIGHTING_GLSL

#include "brdf.glsl"
#include "clusters.glsl"
#include "post.glsl"
#include "shadows.glsl"

// What ShadeSurface shows, by frame.counts.y: the lit surface, the lights-per-cluster heat map, the
// shadow cascade each surface selects, or the ambient access it is shaded with.
const uint kDebugViewShaded = 0u;
const uint kDebugViewClusterHeat = 1u;
const uint kDebugViewShadowCascades = 2u;
const uint kDebugViewAmbientAccess = 3u;

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

// The share of the ambient light that reaches the opaque surface in this pixel, as the ambient
// occlusion passes left it (docs/architecture.md, "Ambient occlusion"), or 1 in a frame without them.
// The image is of the opaque surfaces, so only their programs read it.
layout(set = 0, binding = 16) uniform sampler2D ambientAccessMap;

float SurfaceAmbientAccess()
{
	if (frame.effects.x == 0u)
		return 1.0;
	return texelFetch(ambientAccessMap, ivec2(gl_FragCoord.xy), 0).r;
}

// The environment's light on a surface, by the split sum: the specular lobe's share is the
// prefiltered radiance along the reflection, scaled by the lobe's reflectance under white light. The
// ambient access dims both, the specular by as much of it as the lobe is wide.
vec3 EnvironmentLighting(Surface surface, vec3 normal, vec3 toViewer, float access)
{
	float nDotV = clamp(dot(normal, toViewer), 0.0001, 1.0);
	vec3 reflected = reflect(-toViewer, normal);
	vec2 scaleBias = texture(brdfLut, vec2(nDotV, surface.roughness)).rg;
	vec3 prefiltered =
	    textureLod(environmentSpecular, reflected, surface.roughness * frame.environment.x).rgb;
	vec3 specular = prefiltered * (SpecularColor(surface) * scaleBias.x + scaleBias.y);
	vec3 diffuse = texture(environmentIrradiance, normal).rgb * DiffuseColor(surface);
	return diffuse * access + specular * SpecularAccess(nDotV, access, surface.roughness);
}

// The sun's shadow maps (Render::Renderer's sun-shadows pass): one layer per cascade, reversed
// depth, sampled with a comparison that passes where the receiver is at least as near the sun as
// what the map holds.
layout(set = 0, binding = 10) uniform sampler2DArrayShadow sunShadowMap;

// How far a receiver is moved along its normal before it is looked up, in texels of its cascade, at
// a surface edge-on to the sun; facing the sun, it is not moved. With the shadow pass's slope bias
// this is what keeps a lit surface from shadowing itself.
const float kShadowNormalOffset = 1.5;

// The view depth of a render-space point.
float ViewDepth(vec3 renderPosition)
{
	return -(frame.view * vec4(renderPosition, 1.0)).z;
}

// The cluster a fragment at a view depth falls in, or kClusterCount past the cluster range.
uint FragmentCluster(float depth)
{
	return ClusterAt(
	    gl_FragCoord.xy, depth, frame.clusterTile.xy, frame.clusterDepth.x, frame.clusterDepth.y);
}

// How much of the sun reaches a point in one cascade: the share of a 4 x 4 texel neighbourhood that
// is no further from the sun than the map holds, as nine bilinear comparison taps a texel apart.
float CascadeShadow(uint cascade, vec3 renderPosition, vec3 normal, float sinToSun)
{
	vec3 offset = normal * (frame.shadowTexels[cascade] * kShadowNormalOffset * sinToSun);
	vec3 coordinate = ShadowMapCoordinate(frame.shadowMatrices[cascade], renderPosition + offset);
	vec2 texel = 1.0 / vec2(textureSize(sunShadowMap, 0).xy);
	float lit = 0.0;
	// With explicit gradients: the lookups are in control flow that differs between fragments, where
	// implicit ones are undefined, and the maps have one level.
	for (int y = -1; y <= 1; ++y)
		for (int x = -1; x <= 1; ++x)
			lit += textureGrad(
			    sunShadowMap,
			    vec4(coordinate.xy + vec2(x, y) * texel, float(cascade), coordinate.z), vec2(0.0),
			    vec2(0.0));
	return lit / 9.0;
}

// How much of the sun reaches a surface at a view depth: one where it casts no shadow (none this
// frame, past the shadow distance, or a surface facing away, which the sun does not light anyway),
// and otherwise its cascade's answer, blended into the next cascade's, or out of shadow past the
// last, across the end of the cascade.
float SunShadow(vec3 renderPosition, vec3 normal, float depth)
{
	float nDotL = dot(normal, frame.sunDirection.xyz);
	if (frame.counts.z == 0u || nDotL <= 0.0)
		return 1.0;
	uint cascade = ShadowCascadeAt(depth, frame.shadowSplits);
	if (cascade >= kShadowCascades)
		return 1.0;
	float sinToSun = sqrt(max(1.0 - nDotL * nDotL, 0.0));
	float lit = CascadeShadow(cascade, renderPosition, normal, sinToSun);
	float blend = ShadowCascadeBlend(depth, frame.shadowSplits[cascade]);
	if (blend > 0.0)
	{
		float next = cascade + 1u < kShadowCascades
		                 ? CascadeShadow(cascade + 1u, renderPosition, normal, sinToSun)
		                 : 1.0;
		lit = mix(lit, next, blend);
	}
	return lit;
}

// The cascade view's colour for a view depth: red, green, blue and yellow for the four cascades,
// mixed across the bands where one blends into the next, and grey past the shadow distance.
vec3 CascadeColor(float depth)
{
	const vec3 colors[5] = vec3[5](
	    vec3(1.0, 0.25, 0.2), vec3(0.25, 1.0, 0.25), vec3(0.3, 0.45, 1.0), vec3(1.0, 0.9, 0.2),
	    vec3(0.35));
	uint cascade = ShadowCascadeAt(depth, frame.shadowSplits);
	if (frame.counts.z == 0u || cascade >= kShadowCascades)
		return colors[kShadowCascades];
	return mix(
	    colors[cascade], colors[cascade + 1u],
	    ShadowCascadeBlend(depth, frame.shadowSplits[cascade]));
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

// The light a surface sends towards the camera, which is at the render-space origin, with the sun's
// light by colour the share given of what it is above the air (the planet program's terminator and
// the shadows of rings and clouds; 1 for every other program), and the environment's by the ambient
// access given.
vec3 ShadeSurfaceSunlit(Surface surface, vec3 normal, vec3 renderPosition, vec3 sunlight, float access)
{
	vec3 toViewer = normalize(-renderPosition);
	// An interpolated normal can lean away from a viewer that can still see its triangle; it is
	// bent back to the silhouette rather than shaded black.
	normal = normalize(normal - toViewer * min(dot(normal, toViewer) - 0.001, 0.0));

	float depth = ViewDepth(renderPosition);
	uint cluster = FragmentCluster(depth);
	// Flat colour, darkened as the surface turns away so the shapes still read. The material's
	// emission is still added over it, so a lit window shows through.
	if (frame.counts.y == kDebugViewClusterHeat)
		return ClusterHeat(cluster) * (0.5 + 0.5 * max(dot(normal, toViewer), 0.0));
	// The cascade's colour, lit by the sun alone and shadowed, over a floor that keeps the shadowed
	// side's shapes readable.
	if (frame.counts.y == kDebugViewShadowCascades)
	{
		float sunLight = max(dot(normal, frame.sunDirection.xyz), 0.0) *
		                 SunShadow(renderPosition, normal, depth);
		return CascadeColor(depth) * (0.15 + 0.85 * sunLight);
	}
	if (frame.counts.y == kDebugViewAmbientAccess)
		return vec3(access);

	vec3 radiance = vec3(0.0);
	// The sun is one direction for the whole frame: a star 150 million kilometres off subtends half
	// a degree, and its light does not fall off across a station.
	if (frame.sunDirection.w != 0.0)
		radiance += EvaluateBrdf(surface, normal, toViewer, frame.sunDirection.xyz) *
		            frame.sunColorIntensity.rgb * sunlight *
		            (frame.sunColorIntensity.a * SunShadow(renderPosition, normal, depth));
	// The cluster's lights: those whose range reaches it, and no others. Past the cluster range
	// there are none, and the surface has the sun, the sky and its own emission.
	if (cluster < kClusterCount)
	{
		uvec2 entry = clusterLights[cluster];
		for (uint index = 0u; index < entry.y; ++index)
			radiance += LocalLighting(
			    surface, normal, toViewer, renderPosition, lights[lightIndices[entry.x + index]]);
	}
	return radiance + EnvironmentLighting(surface, normal, toViewer, access);
}

// An opaque surface, under the ambient occlusion of its pixel.
vec3 ShadeSurface(Surface surface, vec3 normal, vec3 renderPosition)
{
	return ShadeSurfaceSunlit(surface, normal, renderPosition, vec3(1.0), SurfaceAmbientAccess());
}

// A transparent one, which the ambient occlusion of the opaque surface behind it does not reach.
vec3 ShadeTransparentSurface(Surface surface, vec3 normal, vec3 renderPosition)
{
	return ShadeSurfaceSunlit(surface, normal, renderPosition, vec3(1.0), 1.0);
}

// The share of the light arriving from the environment that a surface reflects towards the camera,
// by the split sum: what a transparent surface does not let through. The normal is the one
// ShadeSurface shades, bent back to the silhouette the same way.
vec3 SpecularReflectance(Surface surface, vec3 normal, vec3 renderPosition)
{
	vec3 toViewer = normalize(-renderPosition);
	normal = normalize(normal - toViewer * min(dot(normal, toViewer) - 0.001, 0.0));
	float nDotV = clamp(dot(normal, toViewer), 0.0001, 1.0);
	vec2 scaleBias = texture(brdfLut, vec2(nDotV, surface.roughness)).rg;
	return SpecularColor(surface) * scaleBias.x + scaleBias.y;
}

// The light a lit particle sends towards the camera, per unit of its albedo: a small Lambertian
// sphere (LambertSpherePhase) under the sun, shadowed through its cascades, under the lights of the
// cluster it is in, and under the sky, whose irradiance on the half of it the camera sees is taken
// as the irradiance along the way to the camera.
vec3 ShadeVolume(vec3 renderPosition)
{
	vec3 toViewer = normalize(-renderPosition);
	float depth = ViewDepth(renderPosition);
	vec3 radiance = vec3(0.0);
	if (frame.sunDirection.w != 0.0)
	{
		// Looked up with the sun's own direction as the normal, so the receiver is not moved: a
		// particle has no surface to keep off.
		vec3 toSun = frame.sunDirection.xyz;
		radiance += frame.sunColorIntensity.rgb *
		            (frame.sunColorIntensity.a * SunShadow(renderPosition, toSun, depth) *
		             LambertSpherePhase(dot(toSun, toViewer)) / kPi);
	}
	uint cluster = FragmentCluster(depth);
	if (cluster < kClusterCount)
	{
		uvec2 entry = clusterLights[cluster];
		for (uint index = 0u; index < entry.y; ++index)
		{
			Light light = lights[lightIndices[entry.x + index]];
			vec3 toLight = light.positionRange.xyz - renderPosition;
			float distanceSquared = dot(toLight, toLight);
			vec3 lightDirection = toLight * inversesqrt(max(distanceSquared, 1e-12));
			float attenuation = RangeAttenuation(distanceSquared, light.positionRange.w) *
			                    SpotAttenuation(light.directionType.xyz, -lightDirection, light.cone.x,
			                                    light.cone.y);
			radiance += light.colorIntensity.rgb *
			            (light.colorIntensity.a * attenuation *
			             LambertSpherePhase(dot(lightDirection, toViewer)) / kPi);
		}
	}
	return radiance + texture(environmentIrradiance, toViewer).rgb;
}

#endif
