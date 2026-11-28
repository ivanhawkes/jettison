#version 450
#extension GL_GOOGLE_include_directive : require

#include "colour.glsl"

// ImGui's own fragment stage (imgui_impl_vulkan.cpp, glsl_shader.frag), whose interface this keeps,
// with the vertex colour decoded for an attachment that encodes sRGB itself
// (vulkan/imgui/ImGuiRenderer.cpp). The colour arrives in ImGui's 8-bit bytes as the palette writes
// it, so it is decoded here in floats rather than before the packing, where the darkest tones lose
// up to four steps. The texture is not decoded: the font atlas's colour is white, and an image is
// drawn as it was before.
layout(location = 0) out vec4 fColor;
layout(set = 0, binding = 0) uniform texture2D _Texture;
layout(set = 1, binding = 0) uniform sampler _Sampler;
layout(location = 0) in struct
{
	vec4 Color;
	vec2 UV;
} In;

void main()
{
	vec4 color = vec4(SrgbToLinear(In.Color.rgb), In.Color.a);
	fColor = color * texture(sampler2D(_Texture, _Sampler), In.UV.st);
}
