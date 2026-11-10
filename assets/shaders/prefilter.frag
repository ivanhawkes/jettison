#version 450
#extension GL_GOOGLE_include_directive : require

#include "brdf.glsl"
#include "cube.glsl"

// One face of one level of the environment, from the captured sky: built as prefilter_specular
// (SPECULAR), the radiance seen through GGX's lobe at the level's roughness, with the normal, the
// view and the reflection taken to be one direction (the split sum's assumption); built as
// prefilter_irradiance, the cosine-weighted radiance over the hemisphere, over pi, which a
// Lambertian surface multiplies by its diffuse colour.
//
// Both sample the source by filtered importance sampling (Colbert and Krivanek, GPU Gems 3,
// chapter 20): each sample reads the source's mip whose texels are about the solid angle the
// sample stands for, so 64 samples do the work of thousands without aliasing the stars.

layout(set = 0, binding = 0) uniform samplerCube source;

layout(push_constant) uniform Prefilter
{
	uint face;
	uint sampleCount;
	float roughness;
	// The source's texels along a face's edge at level 0, and its last level.
	float sourceSize;
	float sourceMaxLevel;
} prefilter;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

// The mip of the source whose texel covers the solid angle one of the samples stands for.
float SourceLevel(float pdf)
{
	float sampleSolidAngle = 1.0 / (float(prefilter.sampleCount) * max(pdf, 0.000001));
	float texelSolidAngle = 4.0 * kPi / (6.0 * prefilter.sourceSize * prefilter.sourceSize);
	return clamp(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0, prefilter.sourceMaxLevel);
}

void main()
{
	vec3 normal = CubeDirection(prefilter.face, fragUv);
	vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
	vec3 tangent = normalize(cross(up, normal));
	vec3 bitangent = cross(normal, tangent);

	vec3 sum = vec3(0.0);
	float weight = 0.0;
#ifdef SPECULAR
	float alpha = prefilter.roughness * prefilter.roughness;
	if (alpha == 0.0)
	{
		outColor = vec4(textureLod(source, normal, 0.0).rgb, 1.0);
		return;
	}
	for (uint index = 0u; index < prefilter.sampleCount; ++index)
	{
		vec3 h = ImportanceSampleGgx(Hammersley(index, prefilter.sampleCount), alpha);
		vec3 halfway = tangent * h.x + bitangent * h.y + normal * h.z;
		vec3 light = reflect(-normal, halfway);
		float nDotL = dot(normal, light);
		if (nDotL > 0.0)
		{
			// With n = v, the pdf of l is D(h) / 4.
			float pdf = DistributionGgx(h.z, alpha) * 0.25;
			sum += textureLod(source, light, SourceLevel(pdf)).rgb * nDotL;
			weight += nDotL;
		}
	}
#else
	for (uint index = 0u; index < prefilter.sampleCount; ++index)
	{
		vec3 l = ImportanceSampleCosine(Hammersley(index, prefilter.sampleCount));
		vec3 light = tangent * l.x + bitangent * l.y + normal * l.z;
		sum += textureLod(source, light, SourceLevel(l.z / kPi)).rgb;
		weight += 1.0;
	}
#endif
	outColor = vec4(sum / max(weight, 0.0001), 1.0);
}
