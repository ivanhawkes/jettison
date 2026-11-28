#include <jettison/render/Texture.h>

#include <ktx.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace Jettison::Render
{
namespace
{
// The Vulkan format numbers a KTX 2 file names its texels by. The module keeps Vulkan out of this
// file; these are the values of VkFormat, which KTX 2 stores as they are.
constexpr ktx_uint32_t kVkR8G8B8A8Unorm = 37;
constexpr ktx_uint32_t kVkR8G8B8A8Srgb = 43;
constexpr ktx_uint32_t kVkBc7Unorm = 145;
constexpr ktx_uint32_t kVkBc7Srgb = 146;

// The largest width or height a texture is read at. A file's header claims the size its decoders
// allocate, so a claim past it is refused before anything is allocated for it. It is lavapipe's
// maxImageDimension2D, half the RTX 5060 Ti's 32768 (vulkaninfo, 2026-11-28), and four times the
// test content's largest, 4096.
constexpr std::uint32_t kMaxTextureDimension = 16384;

// The largest width or height an image stb_image reads is decoded at: a development texture or a UI
// image, whose mip chain is built here texel by texel, where a shipped texture is KTX 2 and arrives
// with its chain. The test content's largest texture is 4096 a side; a PNG of 16384 a side, a few
// KiB of a fuzzed file, took the image fuzz target over ten seconds to build the chain of (2026-11-28).
constexpr std::uint32_t kMaxImageDimension = 4096;

// RGBA8's, the widest texel a texture is read as.
constexpr std::uint64_t kTexelBytes = 4;

// The most a texture's image data inflates to: a chain of RGBA8 levels at the largest size, its mips
// a third of its first level more.
constexpr std::uint64_t kMaxTextureBytes =
    std::uint64_t{kMaxTextureDimension} * kMaxTextureDimension * kTexelBytes * 4 / 3;


[[noreturn]] void Refuse(std::string_view name, const std::string &what)
{
	throw std::runtime_error("Texture '" + std::string{name} + "': " + what);
}

struct KtxDeleter
{
	void operator()(ktxTexture2 *pTexture) const
	{
		ktxTexture_Destroy(ktxTexture(pTexture));
	}
};


float SrgbToLinear(float value)
{
	return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}


float LinearToSrgb(float value)
{
	return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}


// The 256 sRGB-encoded bytes as linear values, built once: the box filter reads four of them for
// every texel of every level.
const std::array<float, 256> &SrgbTable()
{
	static const std::array<float, 256> table = []
	{
		std::array<float, 256> values{};
		for (std::size_t index = 0; index < values.size(); ++index)
			values[index] = SrgbToLinear(static_cast<float>(index) / 255.0f);
		return values;
	}();
	return table;
}


std::uint8_t ToByte(float value)
{
	return static_cast<std::uint8_t>(std::clamp(std::lround(value * 255.0f), 0L, 255L));
}


// The source texels a destination texel at `index` averages along one axis: two, or three for
// the last one of an odd source, so that no source texel is dropped.
std::pair<std::uint32_t, std::uint32_t> Footprint(
    std::uint32_t index, std::uint32_t source, std::uint32_t destination)
{
	if (source == 1)
		return {0, 1};
	const std::uint32_t first = index * 2;
	const std::uint32_t count = (index + 1 == destination && source % 2 == 1) ? 3 : 2;
	return {first, count};
}


void ProbeSize(std::span<const std::byte> bytes, std::string_view name)
{
	if (bytes.empty())
		Refuse(name, "the file is empty");
	if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		Refuse(name, "the file is too large to decode");
}


// A KTX 2 file's layout (the KTX 2.0 specification, sections 3.1 to 3.5): an 80-byte header whose
// level count is at 40, its index of the data format descriptor's, the key/value data's and the
// supercompression global data's offsets and lengths from 48, then a level index of 24 bytes a level,
// each its offset, length and uncompressed length.
constexpr std::size_t kKtx2HeaderBytes = 80;
constexpr std::size_t kKtx2LevelCountAt = 40;
constexpr std::size_t kKtx2DfdAt = 48;
constexpr std::size_t kKtx2KvdAt = 56;
constexpr std::size_t kKtx2SgdAt = 64;
constexpr std::size_t kKtx2LevelBytes = 24;
constexpr std::size_t kU32 = sizeof(std::uint32_t);
constexpr std::size_t kU64 = sizeof(std::uint64_t);
constexpr unsigned kBitsPerByte = 8;


std::uint64_t LittleEndian(std::span<const std::byte> bytes, std::size_t at, std::size_t width)
{
	std::uint64_t value = 0;
	for (std::size_t index = width; index-- > 0;)
		value = (value << kBitsPerByte) | std::to_integer<std::uint64_t>(bytes[at + index]);
	return value;
}


// Every region a KTX 2 file's index names lies within the file, before libktx reads it: libktx
// allocates each region's claimed length before reading it, and a fuzzed file's supercompression
// global data claimed 0x4100000000000000 bytes (texture2.c, ktxTexture2_constructFromStreamAndHeader,
// 2026-11-28).
void CheckKtx2Layout(std::span<const std::byte> bytes, std::string_view name)
{
	if (bytes.size() < kKtx2HeaderBytes)
		Refuse(name, "not a KTX 2 file it can read: shorter than a KTX 2 header");
	const std::uint64_t levels =
	    std::max<std::uint64_t>(LittleEndian(bytes, kKtx2LevelCountAt, kU32), 1);
	if (levels > (bytes.size() - kKtx2HeaderBytes) / kKtx2LevelBytes)
		Refuse(name, std::format("its {} levels' index does not fit in the file", levels));
	const auto within = [&bytes](std::uint64_t offset, std::uint64_t length)
	{ return offset <= bytes.size() && length <= bytes.size() - offset; };
	const bool regions =
	    within(LittleEndian(bytes, kKtx2DfdAt, kU32), LittleEndian(bytes, kKtx2DfdAt + kU32, kU32)) &&
	    within(LittleEndian(bytes, kKtx2KvdAt, kU32), LittleEndian(bytes, kKtx2KvdAt + kU32, kU32)) &&
	    within(LittleEndian(bytes, kKtx2SgdAt, kU64), LittleEndian(bytes, kKtx2SgdAt + kU64, kU64));
	if (!regions)
		Refuse(name, "its index names data past the end of the file");
	for (std::uint64_t level = 0; level < levels; ++level)
	{
		const std::size_t at = kKtx2HeaderBytes + static_cast<std::size_t>(level) * kKtx2LevelBytes;
		if (!within(LittleEndian(bytes, at, kU64), LittleEndian(bytes, at + kU64, kU64)))
			Refuse(name, std::format("its level {} lies past the end of the file", level));
	}
}


void CheckDimensions(
    std::uint64_t width, std::uint64_t height, std::string_view name,
    std::uint64_t most = kMaxTextureDimension)
{
	if (width > most || height > most)
		Refuse(
		    name, std::format(
		              "it is {} by {}, past the {} a side a texture is read at", width, height, most));
}
} // namespace


bool IsSrgb(TextureFormat format)
{
	return format == TextureFormat::Rgba8Srgb || format == TextureFormat::Bc7Srgb;
}


bool IsBlockCompressed(TextureFormat format)
{
	return format == TextureFormat::Bc7Unorm || format == TextureFormat::Bc7Srgb;
}


TextureData DecodeKtx2(std::span<const std::byte> bytes, TranscodeTarget target, std::string_view name)
{
	ProbeSize(bytes, name);
	ktxTexture2 *pRaw = nullptr;
	CheckKtx2Layout(bytes, name);
	const KTX_error_code created = ktxTexture2_CreateFromMemory(
	    reinterpret_cast<const ktx_uint8_t *>(bytes.data()), bytes.size(), KTX_TEXTURE_CREATE_NO_FLAGS,
	    &pRaw);
	if (created != KTX_SUCCESS)
		Refuse(name, std::string{"not a KTX 2 file it can read: "} + ktxErrorString(created));
	const std::unique_ptr<ktxTexture2, KtxDeleter> pTexture{pRaw};

	if (pTexture->isCubemap || pTexture->isArray || pTexture->numDimensions != 2 ||
	    pTexture->numFaces != 1 || pTexture->numLayers != 1)
		Refuse(name, "only 2D textures are read: no cube maps, arrays or volumes");
	CheckDimensions(pTexture->baseWidth, pTexture->baseHeight, name);

	// libktx allocates the sizes the level index claims as it loads them, so they are held to the
	// bytes the file has, and to what a texture of the largest size inflates to.
	if (ktxTexture_GetDataSize(ktxTexture(pTexture.get())) > bytes.size() ||
	    ktxTexture_GetDataSizeUncompressed(ktxTexture(pTexture.get())) > kMaxTextureBytes)
		Refuse(name, "its levels claim more bytes than the file holds, or than a texture inflates to");
	const KTX_error_code loaded = ktxTexture2_LoadImageData(pTexture.get(), nullptr, 0);
	if (loaded != KTX_SUCCESS)
		Refuse(name, std::string{"its image data does not read: "} + ktxErrorString(loaded));

	if (ktxTexture2_NeedsTranscoding(pTexture.get()))
	{
		const ktx_transcode_fmt_e format =
		    target == TranscodeTarget::Bc7 ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32;
		const KTX_error_code transcoded = ktxTexture2_TranscodeBasis(pTexture.get(), format, 0);
		if (transcoded != KTX_SUCCESS)
			Refuse(
			    name, std::string{"Basis Universal transcode failed: "} + ktxErrorString(transcoded));
	}

	// After a transcode the format is the target's, with the colour space the file declared.
	TextureData texture;
	switch (pTexture->vkFormat)
	{
		case kVkR8G8B8A8Unorm: texture.format = TextureFormat::Rgba8Unorm; break;
		case kVkR8G8B8A8Srgb: texture.format = TextureFormat::Rgba8Srgb; break;
		case kVkBc7Unorm: texture.format = TextureFormat::Bc7Unorm; break;
		case kVkBc7Srgb: texture.format = TextureFormat::Bc7Srgb; break;
		default:
			Refuse(
			    name, "texel format " + std::to_string(pTexture->vkFormat) +
			              " is not Basis Universal, RGBA8 or BC7");
	}

	texture.width = pTexture->baseWidth;
	texture.height = pTexture->baseHeight;
	const ktx_uint8_t *pData = ktxTexture_GetData(ktxTexture(pTexture.get()));
	const ktx_size_t dataSize = ktxTexture_GetDataSize(ktxTexture(pTexture.get()));
	for (ktx_uint32_t level = 0; level < pTexture->numLevels; ++level)
	{
		ktx_size_t offset = 0;
		if (ktxTexture_GetImageOffset(ktxTexture(pTexture.get()), level, 0, 0, &offset) != KTX_SUCCESS)
			Refuse(name, "level " + std::to_string(level) + " has no image");

		// The size is the level's dimensions', not the bytes the file gives it, which may be fewer.
		const ktx_size_t size = ktxTexture_GetImageSize(ktxTexture(pTexture.get()), level);
		if (offset > dataSize || size > dataSize - offset)
			Refuse(name, "level " + std::to_string(level) + " is larger than the data it is given");
		TextureLevel entry;
		entry.width = std::max(1u, texture.width >> level);
		entry.height = std::max(1u, texture.height >> level);
		entry.offset = texture.bytes.size();
		entry.size = size;
		const auto *pLevel = reinterpret_cast<const std::byte *>(pData + offset);
		texture.bytes.insert(texture.bytes.end(), pLevel, pLevel + size);
		texture.levels.push_back(entry);
	}
	if (texture.levels.size() == 1 && !IsBlockCompressed(texture.format))
		BuildMipChain(texture);
	return texture;
}


TextureData DecodeImage(std::span<const std::byte> bytes, bool srgb, std::string_view name)
{
	ProbeSize(bytes, name);
	int width = 0;
	int height = 0;
	int channels = 0;

	// stb_image allocates the size a header claims, up to 2 GiB, before it decodes a texel.
	if (stbi_info_from_memory(
	        reinterpret_cast<const stbi_uc *>(bytes.data()), static_cast<int>(bytes.size()), &width,
	        &height, &channels) == 0)
		Refuse(name, std::string{"stb_image cannot decode it: "} + stbi_failure_reason());
	CheckDimensions(
	    static_cast<std::uint64_t>(width), static_cast<std::uint64_t>(height), name,
	    kMaxImageDimension);
	stbi_uc *pPixels = stbi_load_from_memory(
	    reinterpret_cast<const stbi_uc *>(bytes.data()), static_cast<int>(bytes.size()), &width,
	    &height, &channels, STBI_rgb_alpha);
	if (pPixels == nullptr)
		Refuse(name, std::string{"stb_image cannot decode it: "} + stbi_failure_reason());
	const std::unique_ptr<stbi_uc, void (*)(void *)> pOwned{pPixels, stbi_image_free};

	TextureData texture;
	texture.format = srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
	texture.width = static_cast<std::uint32_t>(width);
	texture.height = static_cast<std::uint32_t>(height);
	const std::size_t size = std::size_t{texture.width} * texture.height * 4;
	const auto *pBytes = reinterpret_cast<const std::byte *>(pPixels);
	texture.bytes.assign(pBytes, pBytes + size);
	texture.levels.push_back({texture.width, texture.height, 0, size});
	BuildMipChain(texture);
	return texture;
}


TextureData DecodeTexture(
    std::span<const std::byte> bytes, std::string_view virtualPath, TranscodeTarget target)
{
	const auto endsWith = [virtualPath](std::string_view suffix)
	{ return virtualPath.size() >= suffix.size() && virtualPath.ends_with(suffix); };
	if (endsWith(".ktx2"))
		return DecodeKtx2(bytes, target, virtualPath);
	const std::size_t extension = virtualPath.rfind('.');
	const bool linear =
	    extension != std::string_view::npos && virtualPath.substr(0, extension).ends_with(".linear");
	return DecodeImage(bytes, !linear, virtualPath);
}


void BuildMipChain(TextureData &texture)
{
	if (IsBlockCompressed(texture.format))
		throw std::logic_error("BuildMipChain: a block-compressed texture cannot be filtered here");
	if (texture.levels.size() != 1)
		throw std::logic_error("BuildMipChain: the texture needs exactly one level");

	const bool srgb = IsSrgb(texture.format);
	const auto &table = SrgbTable();
	while (texture.levels.back().width > 1 || texture.levels.back().height > 1)
	{
		const TextureLevel source = texture.levels.back();
		TextureLevel level;
		level.width = std::max(1u, source.width / 2);
		level.height = std::max(1u, source.height / 2);
		level.offset = texture.bytes.size();
		level.size = std::size_t{level.width} * level.height * 4;
		texture.bytes.resize(texture.bytes.size() + level.size);

		const std::byte *pSource = texture.bytes.data() + source.offset;
		std::byte *pDestination = texture.bytes.data() + level.offset;
		for (std::uint32_t y = 0; y < level.height; ++y)
		{
			const auto [firstRow, rows] = Footprint(y, source.height, level.height);
			for (std::uint32_t x = 0; x < level.width; ++x)
			{
				const auto [firstColumn, columns] = Footprint(x, source.width, level.width);
				std::array<float, 4> sum{};
				for (std::uint32_t row = firstRow; row < firstRow + rows; ++row)
					for (std::uint32_t column = firstColumn; column < firstColumn + columns; ++column)
					{
						const std::byte *pTexel =
						    pSource + (std::size_t{row} * source.width + column) * 4;
						for (std::size_t channel = 0; channel < 4; ++channel)
						{
							const auto value = static_cast<std::uint8_t>(pTexel[channel]);
							sum[channel] +=
							    srgb && channel < 3 ? table[value] : static_cast<float>(value) / 255.0f;
						}
					}
				const auto weight = static_cast<float>(rows * columns);
				std::byte *pTexel = pDestination + (std::size_t{y} * level.width + x) * 4;
				for (std::size_t channel = 0; channel < 4; ++channel)
				{
					const float average = sum[channel] / weight;
					pTexel[channel] =
					    std::byte{ToByte(srgb && channel < 3 ? LinearToSrgb(average) : average)};
				}
			}
		}
		texture.levels.push_back(level);
	}
}


TextureData SolidTexture(
    std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha, bool srgb)
{
	TextureData texture;
	texture.format = srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
	texture.width = 1;
	texture.height = 1;
	texture.bytes = {std::byte{red}, std::byte{green}, std::byte{blue}, std::byte{alpha}};
	texture.levels.push_back({1, 1, 0, 4});
	return texture;
}
} // namespace Jettison::Render
