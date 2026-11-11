// A scene draw's push constants, read by the vertex and fragment stages: Render's DrawPushConstants
// (render/src/vulkan/Renderer.cpp), whose size is asserted there. What differs between the instances
// of one draw is in the instance buffer (mesh_vertex.glsl), not here.

layout(push_constant) uniform Draw
{
	// Where the draw's material parameters start in the frame's material buffer.
	uint materialOffset;
	// The cascade a shadow-depth draw renders into (shadow_depth.vert); other draws ignore it.
	uint shadowCascade;
} draw;
