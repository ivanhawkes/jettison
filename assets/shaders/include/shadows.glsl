// The sun's cascaded shadow maps (docs/architecture.md, "Sun shadows"): the view is cut into
// depth slices from the near plane to the shadow distance, and each slice has a square shadow map
// of its own, one layer of one array image, fitted on the CPU (render/Shadows.h). Here are the
// parts the lighting include and the CPU share: which cascade a view depth selects, how a fragment
// blends from one cascade into the next across the end of the first, and where a render-space
// point falls in a cascade's map. The CPU fits each cascade over the depths that select it, band
// and all, so a fragment always finds its point inside the maps it samples.
//
// It is written in the subset of GLSL that GLM also compiles as C++, as clusters.glsl is: no
// swizzles, no out parameters, no struct constructors, float literals with an f suffix.
//
// A cascade's map is reversed, as the main depth buffer is: depth 1 is the side towards the sun
// and 0 the far side, so a caster is kept by GREATER, and a receiver is lit where its depth is at
// least the map's.

#ifndef JETTISON_SHADOWS_GLSL
#define JETTISON_SHADOWS_GLSL

const uint kShadowCascades = 4u;

// The share of a cascade's far depth, at its end, over which a fragment blends into the next
// cascade, or out of shadow past the last.
const float kShadowBlendBand = 0.1f;

// The cascade a view depth selects: the first whose far depth is beyond it, or kShadowCascades
// past the last, where the sun casts no shadow. The splits are the cascades' far depths.
uint ShadowCascadeAt(float depth, vec4 splits)
{
	for (uint cascade = 0u; cascade < kShadowCascades; ++cascade)
		if (depth < splits[int(cascade)])
			return cascade;
	return kShadowCascades;
}

// The view depth from which a cascade has to cover the view: the near plane for the first, and the
// start of the previous cascade's band for the rest.
float ShadowCascadeStart(uint cascade, vec4 splits, float nearPlane)
{
	if (cascade == 0u)
		return nearPlane;
	return splits[int(cascade) - 1] * (1.0f - kShadowBlendBand);
}

// How far a view depth is into the band at the end of its cascade, whose far depth is `split`: 0
// before the band, rising to 1 at the split, where the next cascade takes over.
float ShadowCascadeBlend(float depth, float split)
{
	float start = split * (1.0f - kShadowBlendBand);
	return clamp((depth - start) / (split - start), 0.0f, 1.0f);
}

// Where a render-space point falls in a cascade's map: x and y the texture coordinate, z the
// reversed depth. The cascade's matrix is orthographic, so clip space is the map's NDC.
vec3 ShadowMapCoordinate(mat4 cascade, vec3 position)
{
	vec4 clip = cascade * vec4(position, 1.0f);
	return vec3(clip.x * 0.5f + 0.5f, clip.y * 0.5f + 0.5f, clip.z);
}

#endif
