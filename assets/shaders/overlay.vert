#version 450
#extension GL_GOOGLE_include_directive : require

#include "overlay.glsl"

// Render::OverlayVertex: pixels from the top left, an sRGB colour with alpha premultiplied, and a
// texture coordinate.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec2 fragUv;

void main()
{
	gl_Position = overlayDraw.transform * vec4(inPosition + overlayDraw.translation, 0.0, 1.0);
	fragColor = inColor;
	fragUv = inUv;
}
