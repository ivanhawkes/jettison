#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "draw.glsl"
#include "material.glsl"

// A fixture of the test content (slots.program.yaml): every one of a program's sixteen texture
// slots, twice what set 1 held before the texture table, each shown unlit in its own cell of a four
// by four grid over the texture coordinates, slot 0 at the lowest u and v, then along u. Its vertex
// stage is mesh.vert built with UNLIT. The slot varies across a draw, so its index is nonuniformEXT,
// and the mip comes from the grid's derivatives, which do not jump at a cell's edge as its
// fractional part does.
const uint kColor = 0u;

const float kCells = 4.0;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 9) flat in vec4 fragEmissiveScale;

layout(location = 0) out vec4 outColor;

void main()
{
	vec2 grid = fragTexCoord * kCells;
	uvec2 cell = uvec2(clamp(grid, vec2(0.0), vec2(kCells - 1.0)));
	uint slot = cell.y * uint(kCells) + cell.x;
	vec4 texel = textureGrad(
	    sampler2D(materialTextures[nonuniformEXT(MaterialTextureIndex(slot))], materialSampler),
	    fract(grid), dFdx(grid), dFdy(grid));
	vec4 color = MaterialParameter(kColor) * fragEmissiveScale;
	outColor = vec4(texel.rgb * fragColor * color.rgb * color.a, 1.0);
}
