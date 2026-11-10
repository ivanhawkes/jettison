#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "frame.glsl"
#include "lighting.glsl"
#include "material.glsl"

// The material's textures (mesh.program.yaml, and mesh_triplanar.program.yaml, which is this file
// built with TRIPLANAR defined).
layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D emissiveTexture;
// glTF's packing: roughness in green, metallic in blue.
layout(set = 1, binding = 2) uniform sampler2D metallicRoughnessTexture;

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
vec4 SampleTriplanar(sampler2D textureSampler, vec3 position, vec3 weights)
{
	return texture(textureSampler, position.zy) * weights.x +
	       texture(textureSampler, position.xz) * weights.y +
	       texture(textureSampler, position.xy) * weights.z;
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
	vec4 baseTexel = SampleTriplanar(baseColorTexture, projected, weights);
	vec3 emissiveTexel = SampleTriplanar(emissiveTexture, projected, weights).rgb;
	vec4 surfaceTexel = SampleTriplanar(metallicRoughnessTexture, projected, weights);
#else
	vec4 baseTexel = texture(baseColorTexture, fragTexCoord);
	vec3 emissiveTexel = texture(emissiveTexture, fragTexCoord).rgb;
	vec4 surfaceTexel = texture(metallicRoughnessTexture, fragTexCoord);
#endif
	vec4 albedo = baseTexel * MaterialParameter(kBaseColor) * vec4(fragColor, 1.0);
	Surface surface = Surface(
	    albedo.rgb, clamp(metallicRoughness.x * surfaceTexel.b, 0.0, 1.0),
	    clamp(metallicRoughness.y * surfaceTexel.g, 0.0, 1.0));
	vec3 lit = ShadeSurface(surface, normalize(fragNormal), fragRenderPosition);
	outColor = vec4(lit + emissiveTexel * emissive.rgb * emissive.a, albedo.a);
}
