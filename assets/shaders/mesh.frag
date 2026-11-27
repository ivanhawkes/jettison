#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "draw.glsl"
#include "frame.glsl"
#include "lighting.glsl"
#include "material.glsl"

// The texture slots (mesh.program.yaml, and mesh_triplanar.program.yaml, which is this file built
// with TRIPLANAR defined).
const uint kBaseColorTexture = 0u;
const uint kEmissiveTexture = 1u;
// The orm map, glTF's packing: occlusion in red (not read yet), roughness in green, metallic in blue.
const uint kOrmTexture = 2u;

// The parameters, in the descriptor's order.
const uint kBaseColor = 0u;
// rgb: colour, a: intensity, multiplied by the emissive texture.
const uint kEmissive = 1u;
// x: metallic, y: perceptual roughness, multiplied by the texture's blue and green.
const uint kMetallicRoughness = 2u;
#ifdef TRIPLANAR
// x: texture repeats per metre, y: how sharply the three projections blend.
const uint kTriplanar = 3u;
#endif

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragRenderPosition;
#ifdef TRIPLANAR
layout(location = 4) in vec3 fragObjectPosition;
layout(location = 5) in vec3 fragObjectNormal;
#endif

layout(location = 0) out vec4 outColor;

#ifdef TRIPLANAR
// The texture projected along each object axis, weighted by how squarely the surface faces it.
vec4 SampleTriplanar(uint slot, vec3 position, vec3 weights)
{
	return MaterialTexture(slot, position.zy) * weights.x +
	       MaterialTexture(slot, position.xz) * weights.y +
	       MaterialTexture(slot, position.xy) * weights.z;
}
#endif

void main()
{
	vec4 emissive = MaterialParameter(kEmissive);
	vec4 metallicRoughness = MaterialParameter(kMetallicRoughness);
#ifdef TRIPLANAR
	vec4 triplanar = MaterialParameter(kTriplanar);
	vec3 weights = pow(abs(normalize(fragObjectNormal)), vec3(max(triplanar.y, 1.0)));
	weights /= max(weights.x + weights.y + weights.z, 0.0001);
	vec3 projected = fragObjectPosition * triplanar.x;
	vec4 baseTexel = SampleTriplanar(kBaseColorTexture, projected, weights);
	vec3 emissiveTexel = SampleTriplanar(kEmissiveTexture, projected, weights).rgb;
	vec4 surfaceTexel = SampleTriplanar(kOrmTexture, projected, weights);
#else
	vec4 baseTexel = MaterialTexture(kBaseColorTexture, fragTexCoord);
	vec3 emissiveTexel = MaterialTexture(kEmissiveTexture, fragTexCoord).rgb;
	vec4 surfaceTexel = MaterialTexture(kOrmTexture, fragTexCoord);
#endif
	vec4 albedo = baseTexel * MaterialParameter(kBaseColor) * vec4(fragColor, 1.0);
	Surface surface = Surface(
	    albedo.rgb, clamp(metallicRoughness.x * surfaceTexel.b, 0.0, 1.0),
	    clamp(metallicRoughness.y * surfaceTexel.g, 0.0, 1.0));
	vec3 lit = ShadeSurface(surface, normalize(fragNormal), fragRenderPosition);
	outColor = vec4(lit + emissiveTexel * emissive.rgb * emissive.a, albedo.a);
}
