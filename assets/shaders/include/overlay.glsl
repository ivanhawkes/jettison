// The overlay's push constants (render/Overlay.h, vulkan/OverlayRenderer.cpp's OverlayPushConstants),
// shared by its two stages.
layout(push_constant) uniform OverlayDraw
{
	// From the frame's pixels to clip space: the frame's projection times the draw's own transform.
	mat4 transform;
	// The draw's offset, in pixels, before the transform.
	vec2 translation;
	// Non-zero when the attachment encodes sRGB itself, so the colours, which are sRGB, are decoded.
	uint decodeSrgb;
} overlayDraw;
