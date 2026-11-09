#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform UniformBufferObject
{
    mat4 view;
    mat4 proj;
    vec4 cameraPosition;
    vec4 lightPosition;
    vec4 lightColorIntensity;
    vec4 ambientColor;
} ubo;

layout(std430, binding = 2) readonly buffer SkinJointPalette
{
    mat4 jointMatrices[];
};

layout(push_constant) uniform DrawTransform
{
    mat4 model;
    uint jointOffset;
    uint skinned;
    uvec2 padding;
} drawTransform;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in uvec4 inJoints1;
layout(location = 7) in vec4 inWeights1;

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
        mat4 skinMatrix = inWeights.x * jointMatrices[drawTransform.jointOffset + inJoints.x] +
                          inWeights.y * jointMatrices[drawTransform.jointOffset + inJoints.y] +
                          inWeights.z * jointMatrices[drawTransform.jointOffset + inJoints.z] +
                          inWeights.w * jointMatrices[drawTransform.jointOffset + inJoints.w] +
                          inWeights1.x * jointMatrices[drawTransform.jointOffset + inJoints1.x] +
                          inWeights1.y * jointMatrices[drawTransform.jointOffset + inJoints1.y] +
                          inWeights1.z * jointMatrices[drawTransform.jointOffset + inJoints1.z] +
                          inWeights1.w * jointMatrices[drawTransform.jointOffset + inJoints1.w];
        localPosition = skinMatrix * localPosition;
        localNormal = mat3(skinMatrix) * localNormal;
    }
    vec4 worldPosition = drawTransform.model * localPosition;
    gl_Position = ubo.proj * ubo.view * worldPosition;
    fragColor = inColor;
    fragTexCoord = inTexCoord;
    fragNormal = normalize(mat3(transpose(inverse(drawTransform.model))) * localNormal);
    fragWorldPosition = worldPosition.xyz;
}