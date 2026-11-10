#version 450
#extension GL_GOOGLE_include_directive : require

#include "frame.glsl"
#include "mesh_vertex.glsl"

layout(push_constant) uniform DrawTransform
{
	mat4 model;
	uint jointOffset;
	uint skinned;
	uvec2 padding;
} drawTransform;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPosition;

void main()
{
	vec4 localPosition = vec4(inPosition, 1.0);
	vec3 localNormal = inNormal;
	if (drawTransform.skinned != 0u)
	{
		uint base = drawTransform.jointOffset;
		mat4 skinMatrix = inWeights.x * jointMatrices[base + inJoints.x] +
		                  inWeights.y * jointMatrices[base + inJoints.y] +
		                  inWeights.z * jointMatrices[base + inJoints.z] +
		                  inWeights.w * jointMatrices[base + inJoints.w] +
		                  inWeights1.x * jointMatrices[base + inJoints1.x] +
		                  inWeights1.y * jointMatrices[base + inJoints1.y] +
		                  inWeights1.z * jointMatrices[base + inJoints1.z] +
		                  inWeights1.w * jointMatrices[base + inJoints1.w];
		localPosition = skinMatrix * localPosition;
		localNormal = mat3(skinMatrix) * localNormal;
	}
	vec4 worldPosition = drawTransform.model * localPosition;
	gl_Position = frame.projection * frame.view * worldPosition;
	fragColor = inColor;
	fragTexCoord = inTexCoord;
	fragNormal = normalize(mat3(transpose(inverse(drawTransform.model))) * localNormal);
	fragWorldPosition = worldPosition.xyz;
}
