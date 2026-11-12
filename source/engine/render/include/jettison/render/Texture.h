#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace Jettison::Render
{
// The texel formats a decoded texture comes in: what the GPU side uploads without converting.
enum class TextureFormat : std::uint8_t
{
	Rgba8Unorm,
	Rgba8Srgb,
	Bc7Unorm,
	Bc7Srgb,
};

// What a Basis Universal texture is transcoded to. Chosen once, from what the device samples:
// BC7 where it has block compression, which every desktop GPU does, and plain RGBA8 elsewhere.
enum class TranscodeTarget : std::uint8_t
{
	Bc7,
	Rgba8,
};

struct TextureLevel
{
	std::uint32_t width{0};
	std::uint32_t height{0};
	// Where the level's texels are in TextureData::bytes.
	std::size_t offset{0};
	std::size_t size{0};
};

// A 2D texture on the CPU, decoded and with its mip chain, ready to be copied to the device level
// by level. Made on the I/O worker: nothing here touches a device.
struct TextureData
{
	TextureFormat format{TextureFormat::Rgba8Srgb};
	std::uint32_t width{0};
	std::uint32_t height{0};
	// The largest first, down to 1x1 for a chain this module built.
	std::vector<TextureLevel> levels;
	std::vector<std::byte> bytes;
};

bool IsSrgb(TextureFormat format);
bool IsBlockCompressed(TextureFormat format);

// A KTX 2 file: Basis Universal (ETC1S or UASTC, either supercompression) is transcoded to the
// target; an uncompressed RGBA8 or a BC7 file is taken as it is. The colour space is the file's,
// from its data format descriptor. A file of one RGBA8 level gets its mip chain built here; a
// block-compressed one keeps the levels it has, since this module does not encode BC7. Cube maps,
// arrays and 3D textures are refused until something draws one. Throws std::runtime_error naming
// the file.
TextureData DecodeKtx2(std::span<const std::byte> bytes, TranscodeTarget target, std::string_view name);

// A PNG, JPEG or other file stb_image reads, for development textures: decoded to RGBA8 with a
// mip chain. sRGB unless the caller says it holds linear data (a normal or a mask).
TextureData DecodeImage(std::span<const std::byte> bytes, bool srgb, std::string_view name);

// By the virtual path's extension: ".ktx2" is DecodeKtx2, anything else DecodeImage, sRGB unless
// the name ends ".linear.<extension>" ("hull-panels-normal.linear.png").
TextureData DecodeTexture(
    std::span<const std::byte> bytes, std::string_view virtualPath, TranscodeTarget target);

// The mip chain of an RGBA8 texture with one level, made with a 2x2 box filter. An sRGB texture is
// averaged in linear light, and alpha always is. An odd dimension folds its last row or column
// into the texel beside it, so every texel of a level is weighed once. Throws std::logic_error for
// a block-compressed texture or one that already has a chain.
void BuildMipChain(TextureData &texture);

// A 1x1 texture of one colour: the stand-in for a texture slot a material leaves empty.
TextureData SolidTexture(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, bool srgb);
} // namespace Jettison::Render
