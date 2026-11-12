// The mesh vertex, as Render::Vertex lays it out (render/vulkan/Model.h).

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;
layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;
layout(location = 6) in uvec4 inJoints1;
layout(location = 7) in vec4 inWeights1;

// The frame's instances: every draw is instanced, and an instance's transform and skin are here,
// found by gl_InstanceIndex, which counts from the draw's first instance. The C++ side is
// Render::GpuInstance (render/vulkan/FrameData.h).
struct Instance
{
	// Object space to render space.
	mat4 model;
	// x: where the instance's joint matrices start in the skin palette, y: 1 when it is skinned,
	// z: one more than its body's index in the frame's body list, 0 for none (frame.glsl).
	uvec4 skin;
};

layout(std430, set = 0, binding = 11) readonly buffer Instances
{
	Instance instances[];
};
