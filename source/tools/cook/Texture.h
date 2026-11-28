#pragma once

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

#include "Settings.h"

namespace Jettison::Cook
{
// Colour (a base colour or an emissive map) is sRGB, and is mipmapped in linear light; every other map
// is data, whose bytes are taken as they are.
enum class ColourSpace
{
	Srgb,
	Linear
};

// An image file stb_image reads (PNG, JPEG, TGA) as KTX 2, Basis Universal UASTC with the settings'
// quality, rate-distortion optimisation, zstd level and mipmaps. One channel stays one (R8), grey with
// alpha becomes RGBA, three and four stay as they are. A KTX 2 file is returned as it is. Encoded on
// one thread, so the same image and settings give the same bytes. Throws std::runtime_error naming the
// image for bytes it cannot decode or encode.
std::vector<std::byte> EncodeTexture(
    std::span<const std::byte> image, std::string_view name, ColourSpace colourSpace,
    const TextureSettings &settings);
} // namespace Jettison::Cook
