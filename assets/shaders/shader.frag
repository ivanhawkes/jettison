#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform sampler2D texSampler;

layout(binding = 0) uniform UniformBufferObject
{
	mat4 view;
	mat4 proj;
	vec4 cameraPosition;
	vec4 lightPosition;
	vec4 lightColorIntensity;
	vec4 ambientColor;
} ubo;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPosition;

layout(location = 0) out vec4 outColor;

void main()
{
	vec4 albedo = texture(texSampler, fragTexCoord) * vec4(fragColor, 1.0);
	vec3 normal = normalize(fragNormal);
	vec3 toLight = ubo.lightPosition.xyz - fragWorldPosition;
	float distanceToLight = length(toLight);
	vec3 lightDirection = toLight / max(distanceToLight, 0.0001);
	vec3 viewDirection = normalize(ubo.cameraPosition.xyz - fragWorldPosition);
	vec3 halfDirection = normalize(lightDirection + viewDirection);
	float diffuse = max(dot(normal, lightDirection), 0.0);
	float specular = pow(max(dot(normal, halfDirection), 0.0), 32.0) * 0.2;
	float attenuation = 1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
	vec3 directLight = ubo.lightColorIntensity.rgb * ubo.lightColorIntensity.a * attenuation *
	                   (diffuse + specular);
	outColor = vec4(albedo.rgb * (ubo.ambientColor.rgb + directLight), albedo.a);
}