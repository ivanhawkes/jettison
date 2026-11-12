#include <jettison/render/Texture.h>

#include <jettison/jobs/IoWorker.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using namespace Jettison::Render;

namespace
{
std::vector<std::byte> ReadBytes(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary};
	REQUIRE(file);
	const std::vector<char> chars{std::istreambuf_iterator<char>{file}, {}};
	std::vector<std::byte> bytes(chars.size());
	for (std::size_t index = 0; index < chars.size(); ++index)
		bytes[index] = static_cast<std::byte>(chars[index]);
	return bytes;
}

// A binary PPM, which stb_image reads like a PNG and a test can write by hand: RGB triples, row by
// row from the top.
std::vector<std::byte> Ppm(
    std::uint32_t width, std::uint32_t height, const std::vector<std::uint8_t> &rgb)
{
	const std::string header =
	    "P6\n" + std::to_string(width) + " " + std::to_string(height) + "\n255\n";
	std::vector<std::byte> bytes;
	for (const char character : header)
		bytes.push_back(static_cast<std::byte>(character));
	for (const std::uint8_t value : rgb)
		bytes.push_back(std::byte{value});
	return bytes;
}

std::uint8_t Texel(const TextureData &texture, std::size_t level, std::size_t index)
{
	return static_cast<std::uint8_t>(texture.bytes[texture.levels[level].offset + index]);
}

void CheckChain(const TextureData &texture, std::size_t bytesPerTexel, std::uint32_t block)
{
	CAPTURE(texture.width);
	CAPTURE(texture.height);
	std::size_t expectedOffset = 0;
	for (std::size_t level = 0; level < texture.levels.size(); ++level)
	{
		const TextureLevel &entry = texture.levels[level];
		CHECK(entry.width == std::max(1u, texture.width >> level));
		CHECK(entry.height == std::max(1u, texture.height >> level));
		const std::size_t blocksWide = (entry.width + block - 1) / block;
		const std::size_t blocksHigh = (entry.height + block - 1) / block;
		CHECK(entry.size == blocksWide * blocksHigh * bytesPerTexel);
		CHECK(entry.offset == expectedOffset);
		expectedOffset += entry.size;
	}
	CHECK(texture.bytes.size() == expectedOffset);
	CHECK(texture.levels.back().width == 1);
	CHECK(texture.levels.back().height == 1);
}

std::filesystem::path Sample(const char *name)
{
	return std::filesystem::path{JETTISON_ASSET_SOURCE_DIR} / "textures" / "hulls" / "hull-panels" /
	       name;
}
} // namespace

TEST_CASE("Texture: the sample KTX 2 files transcode from UASTC and ETC1S to BC7 and to RGBA8")
{
	// The hull plating's colour is UASTC with zstd, its emissive ETC1S: Basis Universal's two modes.
	for (const char *name : {"hull-panels-color.ktx2", "hull-panels-emissive.ktx2"})
	{
		CAPTURE(name);
		const std::vector<std::byte> bytes = ReadBytes(Sample(name));

		const TextureData bc7 = DecodeKtx2(bytes, TranscodeTarget::Bc7, name);
		CHECK(bc7.format == TextureFormat::Bc7Srgb);
		CHECK(bc7.width == 256);
		CHECK(bc7.height == 256);
		CHECK(bc7.levels.size() == 9);
		CheckChain(bc7, 16, 4);

		const TextureData rgba = DecodeKtx2(bytes, TranscodeTarget::Rgba8, name);
		CHECK(rgba.format == TextureFormat::Rgba8Srgb);
		CHECK(rgba.levels.size() == 9);
		CheckChain(rgba, 4, 1);
	}
}

TEST_CASE("Texture: the panels' seams survive the transcode, dark against the plate")
{
	const TextureData texture = DecodeKtx2(
	    ReadBytes(Sample("hull-panels-color.ktx2")), TranscodeTarget::Rgba8, "hull-panels-color");
	// Texel (0, 0) is on a seam, (16, 16) in the middle of a plate.
	const std::size_t plate = (std::size_t{16} * 256 + 16) * 4;
	CHECK(Texel(texture, 0, 0) < 100);
	CHECK(Texel(texture, 0, plate) > Texel(texture, 0, 0) + 40);
	CHECK(Texel(texture, 0, 3) == 255);
}

TEST_CASE("Texture: KTX 2 data it cannot use is refused with the file's name")
{
	const std::vector<std::byte> garbage(64, std::byte{0x2a});
	CHECK_THROWS_WITH_AS(
	    DecodeKtx2(garbage, TranscodeTarget::Bc7, "textures/garbage.ktx2"),
	    doctest::Contains("textures/garbage.ktx2"), std::runtime_error);
	CHECK_THROWS_AS(DecodeKtx2({}, TranscodeTarget::Bc7, "empty"), std::runtime_error);
	CHECK_THROWS_AS(DecodeImage(garbage, true, "garbage.png"), std::runtime_error);
}

TEST_CASE("Texture: a development image gets a mip chain, filtered in linear light when sRGB")
{
	// Black and white side by side: in linear light their average is 0.5, which sRGB encodes as
	// 188; a filter that averaged the encoded bytes would make 128, visibly too dark.
	const std::vector<std::byte> pair = Ppm(2, 1, {0, 0, 0, 255, 255, 255});
	const TextureData srgb = DecodeImage(pair, true, "pair.png");
	CHECK(srgb.format == TextureFormat::Rgba8Srgb);
	REQUIRE(srgb.levels.size() == 2);
	CheckChain(srgb, 4, 1);
	CHECK(Texel(srgb, 1, 0) == 188);
	CHECK(Texel(srgb, 1, 3) == 255);

	const TextureData linear = DecodeImage(pair, false, "pair.linear.png");
	CHECK(linear.format == TextureFormat::Rgba8Unorm);
	CHECK(Texel(linear, 1, 0) == 128);

	// An odd size folds its last column into its neighbour: three texels to one, each weighed once.
	const std::vector<std::byte> odd = Ppm(3, 1, {30, 0, 0, 60, 0, 0, 90, 0, 0});
	const TextureData folded = DecodeImage(odd, false, "odd.png");
	REQUIRE(folded.levels.size() == 2);
	CHECK(folded.levels[1].width == 1);
	CHECK(Texel(folded, 1, 0) == 60);

	const TextureData tall =
	    DecodeImage(Ppm(5, 3, std::vector<std::uint8_t>(std::size_t{5} * 3 * 3, 200)), true, "tall");
	CHECK(tall.levels.size() == 3);
	CheckChain(tall, 4, 1);
	CHECK(Texel(tall, 2, 0) == 200);
}

TEST_CASE("Texture: the decoder is chosen by extension, and .linear names linear data")
{
	const std::vector<std::byte> image = Ppm(1, 1, {10, 20, 30});
	CHECK(
	    DecodeTexture(image, "textures/plate.ppm", TranscodeTarget::Bc7).format ==
	    TextureFormat::Rgba8Srgb);
	CHECK(
	    DecodeTexture(image, "textures/plate_normal.linear.ppm", TranscodeTarget::Bc7).format ==
	    TextureFormat::Rgba8Unorm);
	CHECK_THROWS_AS(
	    DecodeTexture(image, "textures/plate.ktx2", TranscodeTarget::Bc7), std::runtime_error);

	TextureData compressed =
	    DecodeKtx2(ReadBytes(Sample("hull-panels-emissive.ktx2")), TranscodeTarget::Bc7, "lights");
	CHECK_THROWS_AS(BuildMipChain(compressed), std::logic_error);

	const TextureData solid = SolidTexture(1, 2, 3, 4, false);
	CHECK(solid.format == TextureFormat::Rgba8Unorm);
	CHECK(solid.levels.size() == 1);
	CHECK(Texel(solid, 0, 3) == 4);
}

TEST_CASE("Texture: a texture changed on disk reloads through the resource manager")
{
	const std::filesystem::path directory =
	    std::filesystem::temp_directory_path() / ("jettison-texture-tests-" + std::to_string(getpid()));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory / "textures");
	const auto put = [&](const std::vector<std::byte> &bytes)
	{
		std::ofstream file{directory / "textures" / "plate.ppm", std::ios::binary | std::ios::trunc};
		file.write(
		    reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	};
	put(Ppm(2, 2, std::vector<std::uint8_t>(12, 100)));

	{
		Vfs::FileSystem files;
		files.AddMount({"base", directory, false});
		Jobs::IoWorker worker;
		Resource::ResourceManager manager{files, worker, {.hotReload = false, .pollInterval = {}}};
		// The loader the renderer registers, less its GPU finish: decode only.
		manager.RegisterLoader(
		    Resource::Loader<TextureData>{
		        [](std::span<const std::byte> bytes, std::string_view path)
		        {
			        return std::make_unique<TextureData>(
			            DecodeTexture(bytes, path, TranscodeTarget::Bc7));
		        },
		        {},
		        {}});

		const auto handle = manager.Load<TextureData>("textures/plate.ppm");
		worker.WaitIdle();
		manager.Update();
		REQUIRE(manager.TryGet(handle) != nullptr);
		CHECK(manager.TryGet(handle)->width == 2);
		CHECK(manager.RevisionOf(handle) == 1);

		// A different size as well as a different time: the stamp sees either.
		put(Ppm(3, 1, std::vector<std::uint8_t>(9, 50)));
		manager.CheckForChanges();
		worker.WaitIdle();
		manager.Update();
		REQUIRE(manager.TryGet(handle) != nullptr);
		CHECK(manager.TryGet(handle)->width == 3);
		CHECK(manager.TryGet(handle)->levels.size() == 2);
		CHECK(manager.RevisionOf(handle) == 2);
		manager.Release(handle);
	}
	std::error_code error;
	std::filesystem::remove_all(directory, error);
}
