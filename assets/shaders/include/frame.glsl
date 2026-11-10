// The frame: descriptor set 0, written once per frame in flight and bound for every scene program.
// The C++ side is Render::FrameUBO and Render::GpuLight (render/vulkan/FrameData.h), whose sizes
// and offsets are asserted there against this layout. Positions are in render space: metres from
// the camera, which is at the origin.

layout(set = 0, binding = 0) uniform FrameConstants
{
	mat4 view;
	mat4 projection;
	vec4 ambientColor;
	// x: the lights in the light list.
	uvec4 counts;
} frame;

// The skin palette: every skinned draw's joint matrices, one after another. A draw finds its own
// from the offset in its push constants.
layout(std430, set = 0, binding = 1) readonly buffer JointPalette
{
	mat4 jointMatrices[];
};

// The frame's lights, collected on the CPU in render space. Phase 5 assigns them to clusters; until
// then every fragment loops over all of them.
struct Light
{
	vec4 position;
	vec4 colorIntensity;
};

layout(std430, set = 0, binding = 2) readonly buffer LightList
{
	Light lights[];
};
