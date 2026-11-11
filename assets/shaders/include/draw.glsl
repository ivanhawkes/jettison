// A scene draw's push constants, read by the vertex and fragment stages: Render's DrawPushConstants
// (render/src/vulkan/Renderer.cpp), whose size is asserted there.

layout(push_constant) uniform Draw
{
	mat4 model;
	uint jointOffset;
	uint skinned;
	// Where the draw's material parameters start in the frame's material buffer.
	uint materialOffset;
	// The cascade a shadow-depth draw renders into (shadow_depth.vert); other draws ignore it.
	uint shadowCascade;
} draw;
