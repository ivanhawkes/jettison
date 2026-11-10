#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame.glsl"

// The material: set 1, one per primitive.
layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outColor;

void main()
{
	vec4 albedo = texture(baseColorTexture, fragTexCoord) * vec4(fragColor, 1.0);
	vec3 normal = normalize(fragNormal);
	vec3 viewDirection = normalize(frame.cameraPosition.xyz - fragWorldPosition);
	vec3 lit = frame.ambientColor.rgb;
	// The stand-in shading: Blinn-Phong with an unbounded falloff, which the clustered BRDF and
	// finite light ranges of phase 5 retire.
	for (uint index = 0u; index < frame.counts.x; ++index)
	{
		Light light = lights[index];
		vec3 toLight = light.position.xyz - fragWorldPosition;
		float distanceToLight = length(toLight);
		vec3 lightDirection = toLight / max(distanceToLight, 0.0001);
		vec3 halfDirection = normalize(lightDirection + viewDirection);
		float diffuse = max(dot(normal, lightDirection), 0.0);
		float specular = pow(max(dot(normal, halfDirection), 0.0), 32.0) * 0.2;
		float attenuation =
		    1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
		lit += light.colorIntensity.rgb * light.colorIntensity.a * attenuation * (diffuse + specular);
	}
	outColor = vec4(albedo.rgb * lit, albedo.a);
}
