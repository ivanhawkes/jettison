#version 450
#extension GL_GOOGLE_include_directive : require

#include "draw.glsl"
#include "frame.glsl"
#include "mesh_vertex.glsl"

// Built as shadow_depth.vert with SHADOW_DEPTH, the sun's shadow pass: the same position, skinned
// the same way, into the cascade the draw names. Built as depth_prepass.vert with DEPTH_PREPASS, the
// opaque depth prepass: the same position into the view. Neither has a fragment stage, so neither
// writes anything else.
#if defined(SHADOW_DEPTH) || defined(DEPTH_PREPASS)
#define DEPTH_ONLY
#endif

// The prepass's depths are what the opaque pass tests its own against for equality, so every variant
// has to compute the position the same way, whatever else it computes.
invariant gl_Position;

#ifndef DEPTH_ONLY
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
#endif
#if !defined(UNLIT) && !defined(DEPTH_ONLY)
// Built as unlit.vert with UNLIT, for a program that is not lit and so takes neither.
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragRenderPosition;
#endif
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
	Instance instance = instances[gl_InstanceIndex];
	vec4 localPosition = vec4(inPosition, 1.0);
	vec3 localNormal = inNormal;
	if (instance.skin.y != 0u)
	{
		uint base = instance.skin.x;
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
	vec4 renderPosition = instance.model * localPosition;
#ifdef SHADOW_DEPTH
	gl_Position = frame.shadowMatrices[draw.shadowCascade] * renderPosition;
#else
	gl_Position = frame.projection * frame.view * renderPosition;
#endif
#ifndef DEPTH_ONLY
	fragColor = inColor;
	fragTexCoord = inTexCoord;
#endif
#if !defined(UNLIT) && !defined(DEPTH_ONLY)
	fragNormal = normalize(mat3(transpose(inverse(instance.model))) * localNormal);
	fragRenderPosition = renderPosition.xyz;
#endif
#ifdef TRIPLANAR
	vec3 scale = vec3(
	    length(instance.model[0].xyz), length(instance.model[1].xyz), length(instance.model[2].xyz));
	fragObjectPosition = inPosition * scale;
	fragObjectNormal = inNormal;
#endif
}
