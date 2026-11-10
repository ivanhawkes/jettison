// The metallic-roughness BRDF of glTF 2.0 (the specification's appendix B): a GGX distribution,
// the height-correlated Smith visibility term, Schlick's Fresnel approximation, and a Lambertian
// diffuse lobe weighted by what the Fresnel term leaves. Every lit program shades through this
// file, through lighting.glsl, and the image-based lighting is baked through it (brdf_lut.frag,
// prefilter.frag), so the split sum and the direct light agree on one model.
//
// It is written in the subset of GLSL that GLM also compiles as C++: no swizzles, no out
// parameters, no struct constructors, float literals with an f suffix. The render suite includes it
// in a C++ translation unit (BrdfTests.cpp) and checks it against reference values, so the test
// runs this source rather than a copy of it.

#ifndef JETTISON_BRDF_GLSL
#define JETTISON_BRDF_GLSL

const float kPi = 3.14159265f;

// A dielectric's reflectance at normal incidence: glTF's index of refraction of 1.5.
const float kDielectricF0 = 0.04f;

// The smoothest perceptual roughness a direct light is evaluated at. The sun subtends half a degree,
// so a lobe sharper than that shows nothing it does not; and GGX's peak at alpha = 0.0064 times the
// sun's intensity stays well inside the HDR target's half-float range, where a perfect mirror's
// would not.
const float kMinDirectRoughness = 0.08f;

// What a surface is, in the material's terms: the glTF metallic-roughness parameters after the
// textures, with roughness perceptual (alpha is its square).
struct Surface
{
	vec3 baseColor;
	float metallic;
	float roughness;
};

// GGX (Trowbridge-Reitz) normal distribution.
float DistributionGgx(float nDotH, float alpha)
{
	float alpha2 = alpha * alpha;
	float denominator = nDotH * nDotH * (alpha2 - 1.0f) + 1.0f;
	return alpha2 / (kPi * denominator * denominator);
}

// The height-correlated Smith masking-shadowing term with the BRDF's 1 / (4 n.l n.v) folded in,
// which glTF calls V.
float VisibilitySmithGgx(float nDotL, float nDotV, float alpha)
{
	float alpha2 = alpha * alpha;
	float viewTerm = nDotL * sqrt(nDotV * nDotV * (1.0f - alpha2) + alpha2);
	float lightTerm = nDotV * sqrt(nDotL * nDotL * (1.0f - alpha2) + alpha2);
	float sum = viewTerm + lightTerm;
	return sum > 0.0f ? 0.5f / sum : 0.0f;
}

// Schlick's weight of the grazing reflectance (1) against the normal one.
float FresnelWeight(float vDotH)
{
	float m = clamp(1.0f - vDotH, 0.0f, 1.0f);
	float m2 = m * m;
	return m2 * m2 * m;
}

vec3 FresnelSchlick(vec3 f0, float vDotH)
{
	return f0 + (vec3(1.0f) - f0) * FresnelWeight(vDotH);
}

vec3 SpecularColor(Surface surface)
{
	return mix(vec3(kDielectricF0), surface.baseColor, surface.metallic);
}

vec3 DiffuseColor(Surface surface)
{
	return surface.baseColor * (1.0f - surface.metallic);
}

// The BRDF times the cosine of the light's angle: the radiance reflected towards v per unit of
// irradiance arriving along l, perpendicular to it. n, v and l are unit vectors, v and l pointing
// away from the surface. Zero for a light behind the surface.
vec3 EvaluateBrdf(Surface surface, vec3 n, vec3 v, vec3 l)
{
	float nDotL = dot(n, l);
	if (nDotL <= 0.0f)
		return vec3(0.0f);
	float nDotV = max(dot(n, v), 0.0001f);
	vec3 h = normalize(l + v);
	float nDotH = max(dot(n, h), 0.0f);
	float vDotH = max(dot(v, h), 0.0f);
	float roughness = max(surface.roughness, kMinDirectRoughness);
	float alpha = roughness * roughness;

	vec3 fresnel = FresnelSchlick(SpecularColor(surface), vDotH);
	vec3 diffuse = (vec3(1.0f) - fresnel) * DiffuseColor(surface) / kPi;
	vec3 specular =
	    fresnel * (DistributionGgx(nDotH, alpha) * VisibilitySmithGgx(nDotL, nDotV, alpha));
	return (diffuse + specular) * nDotL;
}

// The i-th of n points of the Hammersley set: i / n, and i's bits reversed as a binary fraction.
vec2 Hammersley(uint index, uint count)
{
	uint bits = index;
	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return vec2(float(index) / float(count), float(bits) * 2.3283064365386963e-10f);
}

// A half vector drawn with density D(h) (n.h), in the frame where the normal is +z.
vec3 ImportanceSampleGgx(vec2 xi, float alpha)
{
	float phi = 2.0f * kPi * xi.x;
	float cosTheta = sqrt((1.0f - xi.y) / (1.0f + (alpha * alpha - 1.0f) * xi.y));
	float sinTheta = sqrt(max(1.0f - cosTheta * cosTheta, 0.0f));
	return vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// A direction drawn with density cos(theta) / pi, in the frame where the normal is +z.
vec3 ImportanceSampleCosine(vec2 xi)
{
	float phi = 2.0f * kPi * xi.x;
	float sinTheta = sqrt(xi.y);
	return vec3(sinTheta * cos(phi), sinTheta * sin(phi), sqrt(max(1.0f - xi.y, 0.0f)));
}

// The split sum's second factor (Karis, "Real Shading in Unreal Engine 4", 2013): the specular
// lobe's reflectance under uniform white light, as a scale and a bias on F0, so the environment's
// specular term is prefiltered radiance times (F0 * x + y). Integrated over GGX's half vectors with
// a Hammersley set, at the perceptual roughness given, with no floor: the bake reaches a mirror.
vec2 IntegrateSpecularBrdf(float nDotV, float roughness, uint sampleCount)
{
	vec3 v = vec3(sqrt(max(1.0f - nDotV * nDotV, 0.0f)), 0.0f, nDotV);
	float alpha = roughness * roughness;
	float scale = 0.0f;
	float bias = 0.0f;
	for (uint index = 0u; index < sampleCount; ++index)
	{
		vec3 h = ImportanceSampleGgx(Hammersley(index, sampleCount), alpha);
		float vDotH = dot(v, h);
		vec3 l = h * (2.0f * vDotH) - v;
		float nDotL = l.z;
		float nDotH = h.z;
		if (nDotL > 0.0f && vDotH > 0.0f && nDotH > 0.0f)
		{
			// The estimator for f cos / pdf with pdf(l) = D (n.h) / (4 v.h): D cancels.
			float weight = VisibilitySmithGgx(nDotL, nDotV, alpha) * 4.0f * nDotL * vDotH / nDotH;
			float fresnel = FresnelWeight(vDotH);
			scale += (1.0f - fresnel) * weight;
			bias += fresnel * weight;
		}
	}
	return vec2(scale, bias) / float(sampleCount);
}

#endif
