#include "Texture.h"

#include <ktx.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>
#include <string>

namespace Jettison::Cook
{
namespace
{
// VkFormat's values (the Vulkan registry), which libktx takes as integers: a CPU tree has no Vulkan
// headers.
constexpr std::uint32_t kR8Unorm = 9;
constexpr std::uint32_t kR8Srgb = 15;
constexpr std::uint32_t kR8G8B8Unorm = 23;
constexpr std::uint32_t kR8G8B8Srgb = 29;
constexpr std::uint32_t kR8G8B8A8Unorm = 37;
constexpr std::uint32_t kR8G8B8A8Srgb = 43;

constexpr int kGreyAlphaChannels = 2;

constexpr int kRgbaChannels = 4;

// The fourth channel, alpha, is linear in an sRGB image.
constexpr int kAlphaChannel = 3;

constexpr float kByteMax = 255.0f;

// The sRGB transfer function's constants (IEC 61966-2-1).
constexpr float kSrgbLinearLimit = 0.04045f;
constexpr float kLinearSrgbLimit = 0.0031308f;
constexpr float kSrgbLinearSlope = 12.92f;
constexpr float kSrgbOffset = 0.055f;
constexpr float kSrgbScale = 1.055f;
constexpr float kSrgbGamma = 2.4f;

// What KTXwriter, the file's own record of what wrote it, says.
constexpr std::string_view kWriter = "Jettison's cook (jettison_cook)";

// A KTX 2 file's first twelve bytes.
constexpr std::array<unsigned char, 12> kKtx2Identifier{0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                                        0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};

struct Level
{
	std::uint32_t width{};
	std::uint32_t height{};
	std::vector<std::uint8_t> pixels;
};


float ToLinear(std::uint8_t value)
{
	const float colour = static_cast<float>(value) / kByteMax;
	return colour <= kSrgbLinearLimit ? colour / kSrgbLinearSlope
	                                  : std::pow((colour + kSrgbOffset) / kSrgbScale, kSrgbGamma);
}


std::uint8_t ToSrgb(float linear)
{
	const float colour = linear <= kLinearSrgbLimit
	                         ? linear * kSrgbLinearSlope
	                         : (kSrgbScale * std::pow(linear, 1.0f / kSrgbGamma)) - kSrgbOffset;
	return static_cast<std::uint8_t>(std::lround(std::clamp(colour, 0.0f, 1.0f) * kByteMax));
}


// The level below: each pixel the mean of the two by two above it, an edge's repeated where a side is
// odd, colour averaged in linear light.
Level Halve(const Level &level, int channels, ColourSpace colourSpace)
{
	Level half{std::max(level.width / 2, 1u), std::max(level.height / 2, 1u), {}};
	half.pixels.resize(std::size_t{half.width} * half.height * static_cast<std::size_t>(channels));
	const auto at = [&](std::uint32_t x, std::uint32_t y, int channel)
	{
		x = std::min(x, level.width - 1);
		y = std::min(y, level.height - 1);
		return level.pixels
		    [(((std::size_t{y} * level.width) + x) * static_cast<std::size_t>(channels)) +
		     static_cast<std::size_t>(channel)];
	};
	for (std::uint32_t y = 0; y < half.height; ++y)
		for (std::uint32_t x = 0; x < half.width; ++x)
			for (int channel = 0; channel < channels; ++channel)
			{
				const std::array<std::uint8_t, 4> values{
				    at(2 * x, 2 * y, channel), at((2 * x) + 1, 2 * y, channel),
				    at(2 * x, (2 * y) + 1, channel), at((2 * x) + 1, (2 * y) + 1, channel)};
				const bool colour = colourSpace == ColourSpace::Srgb && channel != kAlphaChannel;
				std::uint8_t mean{};
				if (colour)
				{
					float sum = 0.0f;
					for (const std::uint8_t value : values)
						sum += ToLinear(value);
					mean = ToSrgb(sum / static_cast<float>(values.size()));
				}
				else
				{
					unsigned sum = 0;
					for (const std::uint8_t value : values)
						sum += value;
					mean = static_cast<std::uint8_t>((sum + (values.size() / 2)) / values.size());
				}
				half.pixels
				    [(((std::size_t{y} * half.width) + x) * static_cast<std::size_t>(channels)) +
				     static_cast<std::size_t>(channel)] = mean;
			}
	return half;
}


std::uint32_t Format(int channels, ColourSpace colourSpace)
{
	const bool srgb = colourSpace == ColourSpace::Srgb;
	if (channels == 1)
		return srgb ? kR8Srgb : kR8Unorm;
	if (channels == kRgbaChannels)
		return srgb ? kR8G8B8A8Srgb : kR8G8B8A8Unorm;
	return srgb ? kR8G8B8Srgb : kR8G8B8Unorm;
}


struct TextureDeleter
{
	void operator()(ktxTexture2 *pTexture) const
	{
		ktxTexture2_Destroy(pTexture);
	}
};


void Check(KTX_error_code result, std::string_view name, std::string_view step)
{
	if (result != KTX_SUCCESS)
		throw std::runtime_error(
		    std::format(
		        "Cannot encode {} as KTX 2: {} failed ({})", name, step, ktxErrorString(result)));
}
} // namespace


std::vector<std::byte> EncodeTexture(
    std::span<const std::byte> image, std::string_view name, ColourSpace colourSpace,
    const TextureSettings &settings)
{
	if (image.size() >= kKtx2Identifier.size() &&
	    std::memcmp(image.data(), kKtx2Identifier.data(), kKtx2Identifier.size()) == 0)
		return {image.begin(), image.end()};

	int width = 0;
	int height = 0;
	int channels = 0;
	const auto *pImage = reinterpret_cast<const stbi_uc *>(image.data());
	const int length = static_cast<int>(image.size());
	if (stbi_info_from_memory(pImage, length, &width, &height, &channels) == 0)
		throw std::runtime_error(std::format("Cannot decode {}: {}", name, stbi_failure_reason()));
	const int wanted = channels == kGreyAlphaChannels ? kRgbaChannels : 0;
	const std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pPixels{
	    stbi_load_from_memory(pImage, length, &width, &height, &channels, wanted), &stbi_image_free};
	if (!pPixels)
		throw std::runtime_error(std::format("Cannot decode {}: {}", name, stbi_failure_reason()));
	if (wanted != 0)
		channels = wanted;

	std::vector<Level> levels(1);
	levels[0].width = static_cast<std::uint32_t>(width);
	levels[0].height = static_cast<std::uint32_t>(height);
	levels[0].pixels.assign(
	    pPixels.get(),
	    pPixels.get() + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
	                     static_cast<std::size_t>(channels)));
	while (settings.mipmaps && (levels.back().width > 1 || levels.back().height > 1))
		levels.push_back(Halve(levels.back(), channels, colourSpace));

	ktxTextureCreateInfo info{};
	info.vkFormat = Format(channels, colourSpace);
	info.baseWidth = levels[0].width;
	info.baseHeight = levels[0].height;
	info.baseDepth = 1;
	info.numDimensions = 2;
	info.numLevels = static_cast<ktx_uint32_t>(levels.size());
	info.numLayers = 1;
	info.numFaces = 1;
	info.isArray = KTX_FALSE;
	info.generateMipmaps = KTX_FALSE;
	ktxTexture2 *pCreated = nullptr;
	Check(ktxTexture2_Create(&info, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &pCreated), name, "creating it");
	const std::unique_ptr<ktxTexture2, TextureDeleter> pTexture{pCreated};
	for (std::size_t index = 0; index < levels.size(); ++index)
		Check(
		    ktxTexture_SetImageFromMemory(
		        ktxTexture(pTexture.get()), static_cast<ktx_uint32_t>(index), 0, 0,
		        levels[index].pixels.data(), levels[index].pixels.size()),
		    name, "setting a level");
	const std::string writer{kWriter};
	Check(
	    ktxHashList_AddKVPair(
	        &pTexture->kvDataHead, KTX_WRITER_KEY, static_cast<ktx_uint32_t>(writer.size() + 1),
	        writer.c_str()),
	    name, "naming its writer");

	// One thread, and RDO on one too, which libktx documents as what makes the encoding deterministic.
	ktxBasisParams params{};
	params.structSize = sizeof params;
	params.uastc = KTX_TRUE;
	params.threadCount = 1;
	params.uastcFlags = settings.quality;
	params.uastcRDO = settings.rdo ? KTX_TRUE : KTX_FALSE;
	params.uastcRDONoMultithreading = KTX_TRUE;
	Check(ktxTexture2_CompressBasisEx(pTexture.get(), &params), name, "UASTC encoding");
	Check(ktxTexture2_DeflateZstd(pTexture.get(), settings.zstdLevel), name, "zstd");

	ktx_uint8_t *pBytes = nullptr;
	ktx_size_t size = 0;
	Check(ktxTexture_WriteToMemory(ktxTexture(pTexture.get()), &pBytes, &size), name, "writing it");
	const std::unique_ptr<ktx_uint8_t, decltype(&std::free)> pOwned{pBytes, &std::free};
	const auto *pFirst = reinterpret_cast<const std::byte *>(pBytes);
	return {pFirst, pFirst + size};
}
} // namespace Jettison::Cook
