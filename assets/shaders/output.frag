#version 450
#extension GL_GOOGLE_include_directive : require

#include "colour.glsl"
#include "post.glsl"
#include "tonemap.glsl"

// The resolved HDR target, linear light; the bloom chain's top level, which holds every level's
// light (post.glsl's ApplyBloom); the stencil, where post-processing leaves a pixel alone; and the
// colour grade's table, from the display's encoded colours to the graded ones (render/ColourGrade.h).
layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D bloom;
layout(set = 1, binding = 2) uniform usampler2D stencil;
layout(set = 1, binding = 3) uniform sampler3D grade;

layout(push_constant) uniform Output
{
	// A colour over the scene and how much of it, from 0 to 1: a jump's effect.
	vec4 fade;
	// x: the exposure, y: the bloom's strength, 0 for none; z: 1 when the swapchain is UNORM and this
	// pass has to do the sRGB encoding itself; w: the grade's strength, 0 for none.
	vec4 settings;
	// x: the letterbox's bars, each a share of the frame's height.
	vec4 frame;
} outputSettings;

layout(location = 0) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

const uint kStencilUnprocessed = 4u;

void main()
{
	vec3 scene = texture(hdrColor, fragUv).rgb;
	float strength = outputSettings.settings.y;
	if (strength > 0.0 &&
	    (texelFetch(stencil, ivec2(gl_FragCoord.xy), 0).r & kStencilUnprocessed) == 0u)
		scene = ApplyBloom(scene, texture(bloom, fragUv).rgb, strength);
	vec3 exposed = max(scene * outputSettings.settings.x, vec3(0.0));
	vec3 display = clamp(TonemapPbrNeutral(exposed), 0.0, 1.0);
	// Graded as the display shows it, encoded, as a grading tool grades a screenshot.
	float gradeStrength = outputSettings.settings.w;
	if (gradeStrength > 0.0)
	{
		vec3 encoded = LinearToSrgb(display);
		vec3 graded =
		    texture(grade, LutCoordinate(encoded, float(textureSize(grade, 0).x))).rgb;
		display = SrgbToLinear(clamp(mix(encoded, graded, gradeStrength), 0.0, 1.0));
	}
	display = mix(display, outputSettings.fade.rgb, outputSettings.fade.a);
	// Whole rows, so a bar's edge is the same on every frame of a sequence that holds it.
	float height = float(textureSize(hdrColor, 0).y);
	float bar = floor(outputSettings.frame.x * height + 0.5);
	if (gl_FragCoord.y < bar || gl_FragCoord.y > height - bar)
		display = vec3(0.0);
	outColor = vec4(outputSettings.settings.z != 0.0 ? LinearToSrgb(display) : display, 1.0);
}
