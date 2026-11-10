// The material: set 1 holds its textures, bound in the order its program's descriptor
// (shaders/<program>.program.yaml) lists the slots, and the frame's material buffer holds its
// parameters, one vec4 each in the descriptor's order, from the draw's materialOffset.

layout(std430, set = 0, binding = 3) readonly buffer MaterialParameters
{
	vec4 materialParameters[];
};

vec4 MaterialParameter(uint index)
{
	return materialParameters[draw.materialOffset + index];
}
