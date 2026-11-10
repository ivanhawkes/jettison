#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "frame.glsl"
#include "material.glsl"

// The material's textures (mesh.program.yaml, and mesh_triplanar.program.yaml, which is this file
// built with TRIPLANAR defined).
layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;
layout(set = 1, binding = 1) uniform sampler2D emissiveTexture;

// The parameters, in the descriptor's order.
const uint kBaseColor = 0u;
// rgb: colour, a: intensity, multiplied by the emissive texture.
const uint kEmissive = 1u;
// x: specular strength, y: specular exponent.
const uint kSurface = 2u;
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
	vec4 surface = MaterialParameter(kSurface);
	vec4 emissive = MaterialParameter(kEmissive);
#ifdef TRIPLANAR
	vec4 triplanar = MaterialParameter(kTriplanar);
	vec3 weights = pow(abs(normalize(fragObjectNormal)), vec3(max(triplanar.y, 1.0)));
	weights /= max(weights.x + weights.y + weights.z, 0.0001);
	vec3 projected = fragObjectPosition * triplanar.x;
	vec4 baseTexel = SampleTriplanar(baseColorTexture, projected, weights);
	vec3 emissiveTexel = SampleTriplanar(emissiveTexture, projected, weights).rgb;
#else
	vec4 baseTexel = texture(baseColorTexture, fragTexCoord);
	vec3 emissiveTexel = texture(emissiveTexture, fragTexCoord).rgb;
#endif
	vec4 albedo = baseTexel * MaterialParameter(kBaseColor) * vec4(fragColor, 1.0);
	vec3 normal = normalize(fragNormal);
	vec3 viewDirection = normalize(-fragRenderPosition);
	float specularStrength = surface.x;
	float shininess = max(surface.y, 1.0);
	vec3 lit = frame.ambientColor.rgb;
	// The sun is one direction for the whole frame: a star 150 million kilometres off subtends half
	// a degree, and its light does not fall off across a station.
	if (frame.sunDirection.w != 0.0)
	{
		vec3 sunDirection = frame.sunDirection.xyz;
		vec3 halfDirection = normalize(sunDirection + viewDirection);
		float diffuse = max(dot(normal, sunDirection), 0.0);
		float specular = pow(max(dot(normal, halfDirection), 0.0), shininess) * specularStrength;
		lit += frame.sunColorIntensity.rgb * frame.sunColorIntensity.a * (diffuse + specular);
	}
	// The stand-in shading: Blinn-Phong with an unbounded falloff, which the clustered BRDF and
	// finite light ranges of phase 5 retire.
	for (uint index = 0u; index < frame.counts.x; ++index)
	{
		Light light = lights[index];
		vec3 toLight = light.position.xyz - fragRenderPosition;
		float distanceToLight = length(toLight);
		vec3 lightDirection = toLight / max(distanceToLight, 0.0001);
		vec3 halfDirection = normalize(lightDirection + viewDirection);
		float diffuse = max(dot(normal, lightDirection), 0.0);
		float specular = pow(max(dot(normal, halfDirection), 0.0), shininess) * specularStrength;
		float attenuation =
		    1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
		lit += light.colorIntensity.rgb * light.colorIntensity.a * attenuation * (diffuse + specular);
	}
	outColor = vec4(albedo.rgb * lit + emissiveTexel * emissive.rgb * emissive.a, albedo.a);
}
