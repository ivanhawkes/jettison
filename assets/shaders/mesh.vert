#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "frame.glsl"
#include "mesh_vertex.glsl"

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragRenderPosition;
#ifdef TRIPLANAR
// Triplanar mapping projects along the object's own axes, in the bind pose before skinning, so the
// texture stays on the surface as the object moves and turns. The position is in metres along
// those axes - object space times the transform's scale - so a box stretched to a 60-metre hull
// takes its plating at one size on every face, and the repeat rate is per metre.
layout(location = 4) out vec3 fragObjectPosition;
layout(location = 5) out vec3 fragObjectNormal;
#endif

void main()
{
	vec4 localPosition = vec4(inPosition, 1.0);
	vec3 localNormal = inNormal;
	if (draw.skinned != 0u)
	{
		uint base = draw.jointOffset;
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
	vec4 renderPosition = draw.model * localPosition;
	gl_Position = frame.projection * frame.view * renderPosition;
	fragColor = inColor;
	fragTexCoord = inTexCoord;
	fragNormal = normalize(mat3(transpose(inverse(draw.model))) * localNormal);
	fragRenderPosition = renderPosition.xyz;
#ifdef TRIPLANAR
	vec3 scale = vec3(length(draw.model[0].xyz), length(draw.model[1].xyz), length(draw.model[2].xyz));
	fragObjectPosition = inPosition * scale;
	fragObjectNormal = inNormal;
#endif
}
