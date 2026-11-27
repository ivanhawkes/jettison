// The material: a block in the frame's material buffer from the draw's materialOffset, its texture
// slots first, one index into the texture table each, packed four to a uvec4, then its parameters,
// one vec4 each in the program's descriptor's order (shaders/<program>.program.yaml). Render's
// MaterialCache writes it. A program that includes this enables GL_EXT_nonuniform_qualifier, which
// the table's unsized array needs.
//
// Every draw has one material, its offset a push constant, so a slot's index is the same across the
// draw and needs no nonuniformEXT.

layout(std430, set = 0, binding = 3) readonly buffer MaterialBlocks
{
	uvec4 materialWords[];
};

// The texture table: every material texture, at the index its resource took when it finished, and
// the one sampler they are read with. Elements no texture holds are unbound.
layout(set = 1, binding = 0) uniform texture2D materialTextures[];
layout(set = 1, binding = 1) uniform sampler materialSampler;

// Render::kMaterialTextureSlots (render/Material.h), and the uvec4s its indices take.
const uint kMaterialTextureSlots = 16u;
const uint kMaterialSlotVectors = kMaterialTextureSlots / 4u;

vec4 MaterialParameter(uint index)
{
	return uintBitsToFloat(materialWords[draw.materialOffset + kMaterialSlotVectors + index]);
}

uint MaterialTextureIndex(uint slot)
{
	return materialWords[draw.materialOffset + slot / 4u][slot % 4u];
}

vec4 MaterialTexture(uint slot, vec2 uv)
{
	return texture(sampler2D(materialTextures[MaterialTextureIndex(slot)], materialSampler), uv);
}

vec4 MaterialTextureGrad(uint slot, vec2 uv, vec2 uvDx, vec2 uvDy)
{
	return textureGrad(
	    sampler2D(materialTextures[MaterialTextureIndex(slot)], materialSampler), uv, uvDx, uvDy);
}
