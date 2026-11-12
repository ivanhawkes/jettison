// The frame's outlines (docs/architecture.md, "Outlines"), as the CPU lists them
// (Render::GpuOutline): an object's ID names its outline by index plus one (post.glsl's
// ObjectOutline).

#ifndef JETTISON_OUTLINES_GLSL
#define JETTISON_OUTLINES_GLSL

struct Outline
{
	// rgb: linear colour, a: intensity.
	vec4 color;
	// x: 0 for the selected's line, 1 for the targeted's glow; y: the glow's width in pixels.
	vec4 style;
};

layout(std430, set = 0, binding = 15) readonly buffer OutlineList
{
	Outline outlines[];
};

const float kOutlineSelected = 0.0;
const float kOutlineTargeted = 1.0;

#endif
