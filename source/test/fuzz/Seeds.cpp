// The fuzz targets' seed corpora (docs/build-and-gates.md, "Fuzzing"), written at build time, a
// directory a target: the repository's own files of each format, small enough to mutate quickly, and
// files made here of the formats the repository holds none of, or whose writers are the engine's
// (packs, saves, collision files, cooked models, messages) and images of each kind stb_image reads.
//
// Usage: jettison_fuzz_seeds <repository> <seeds directory>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/geometry/Cook.h>
#include <jettison/geometry/GltfFile.h>
#include <jettison/physics/CollisionFile.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/vfs/Pack.h>

#include "Bake.h"
#include "Wire.h"
#include "World.h"

using namespace Jettison;
namespace fs = std::filesystem;

namespace
{
// The largest file of the repository's taken as a seed: libFuzzer mutates up to the largest input
// it is given, and an input of a few hundred KiB already takes milliseconds to read.
constexpr std::uintmax_t kMaxSeedBytes = std::uintmax_t{256} * 1024;

// The head of the test content's music, whose frames are all alike to a decoder.
constexpr std::size_t kMusicHeadBytes = std::size_t{64} * 1024;

// Where a KTX 2 header holds its width and height, and the largest a seed's may be.
constexpr std::size_t kKtxWidthAt = 20;
constexpr std::size_t kKtxHeightAt = 24;
constexpr std::uint32_t kMaxTextureSide = 256;

constexpr int kImageSide = 8;
constexpr int kJpegQuality = 90;
constexpr int kSteps = 10;

// An MPEG-1 Layer III frame at 128 kbit/s and 44.1 kHz, its header and 413 bytes of silence: the
// test content has no MP3, and no encoder is in the build.
constexpr std::array<std::uint8_t, 4> kMp3FrameHeader{0xff, 0xfb, 0x90, 0x00};
constexpr std::size_t kMp3FrameBytes = 417;
constexpr int kMp3Frames = 8;


std::vector<std::byte> ReadFile(const fs::path &path)
{
	std::ifstream in{path, std::ios::binary};
	const std::string text{std::istreambuf_iterator<char>{in}, {}};
	const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
	return {bytes.begin(), bytes.end()};
}


void WriteFile(const fs::path &path, std::span<const std::byte> bytes)
{
	fs::create_directories(path.parent_path());
	std::ofstream out{path, std::ios::binary | std::ios::trunc};
	out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}


std::span<const std::byte> AsBytes(std::string_view text)
{
	return std::as_bytes(std::span{text.data(), text.size()});
}


// The files under the directory with one of the extensions and at most kMaxSeedBytes, sorted.
std::vector<fs::path> FilesUnder(
    const fs::path &directory, std::initializer_list<std::string_view> extensions)
{
	std::vector<fs::path> found;
	for (const fs::directory_entry &entry : fs::recursive_directory_iterator{directory})
	{
		const std::string name = entry.path().filename().string();
		const bool wanted = std::ranges::any_of(
		    extensions, [&name](std::string_view extension) { return name.ends_with(extension); });
		if (entry.is_regular_file() && wanted && entry.file_size() <= kMaxSeedBytes)
			found.push_back(entry.path());
	}
	std::ranges::sort(found);
	return found;
}


// Each file copied into the corpus under its path from the directory, its separators made dashes.
void Copy(
    const fs::path &directory, std::initializer_list<std::string_view> extensions,
    const fs::path &corpus)
{
	for (const fs::path &file : FilesUnder(directory, extensions))
	{
		std::string name = fs::relative(file, directory).string();
		std::ranges::replace(name, '/', '-');
		fs::create_directories(corpus);
		fs::copy_file(file, corpus / name, fs::copy_options::overwrite_existing);
	}
}


// The textures of at most kMaxTextureSide a side: a larger one takes seconds to transcode under the
// sanitisers, and each mutation of it as long (the floor grid, 2048 square, took ten).
void Textures(const fs::path &directory, const fs::path &corpus)
{
	fs::create_directories(corpus);
	for (const fs::path &file : FilesUnder(directory, {".ktx2"}))
	{
		const std::vector<std::byte> bytes = ReadFile(file);
		const auto side = [&bytes](std::size_t at)
		{
			std::uint32_t value = 0;
			for (std::size_t index = sizeof(value); index-- > 0;)
				value = (value << 8) | std::to_integer<std::uint32_t>(bytes.at(at + index));
			return value;
		};
		if (side(kKtxWidthAt) <= kMaxTextureSide && side(kKtxHeightAt) <= kMaxTextureSide)
			fs::copy_file(file, corpus / file.filename(), fs::copy_options::overwrite_existing);
	}
}


// The models as they are, and as the cook writes them: quantised, compressed and in levels.
void Models(const fs::path &repository, const fs::path &corpus)
{
	for (const fs::path &directory :
	     {repository / "source/test/content/models",
	      repository / "source/tools/collision/tests/models"})
	{
		Copy(directory, {".glb", ".gltf"}, corpus);
		for (const fs::path &model : FilesUnder(directory, {".glb", ".gltf"}))
		{
			const std::vector<std::byte> bytes = ReadFile(model);
			const std::unique_ptr<Geometry::GltfFile> pSource =
			    Geometry::GltfFile::Parse(bytes, model.filename().string(), model.parent_path());
			WriteFile(
			    corpus / ("cooked-" + model.stem().string() + ".glb"),
			    Geometry::CookModel(*pSource, {}, "fuzz-seed"));
		}
	}
}


// The models' collision shapes as the cook bakes them, and a compound of every kind a collision
// file holds.
void CollisionFiles(const fs::path &repository, const fs::path &corpus)
{
	for (const fs::path &model :
	     FilesUnder(repository / "source/tools/collision/tests/models", {".gltf", ".glb"}))
		WriteFile(
		    corpus / (model.stem().string() + ".collision"),
		    Physics::WriteCollisionFile(Bake::CollisionShape(model), model.string()));
	Physics::ShapeDefinition hull;
	hull.kind = Physics::ShapeKind::ConvexHull;
	hull.points = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
	Physics::ShapeDefinition mesh;
	mesh.kind = Physics::ShapeKind::Mesh;
	mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}};
	mesh.indices = {0, 2, 1, 1, 2, 3};
	Physics::ShapeDefinition compound;
	compound.kind = Physics::ShapeKind::Compound;
	compound.parts = {hull, mesh};
	WriteFile(
	    corpus / "compound.collision", Physics::WriteCollisionFile(compound, "compound.collision"));
}


// A save of the save target's world with a body falling in it, and a game's section.
void Saves(const fs::path &corpus)
{
	Sim::SimWorld world{Fuzz::kStep, Fuzz::kSeed};
	Fuzz::Compose(world);
	entt::registry &registry = world.Registry();
	const entt::entity crate = registry.create();
	registry.emplace<Sim::Transform>(crate, Sim::Transform{.position = {0.0, 10.0, 0.0}});
	registry.emplace<Sim::InFrame>(crate, Sim::InFrame{Sim::FrameId("system")});
	registry.emplace<Physics::Body>(crate, Physics::Body{.shape = Fuzz::kCrate, .mass = 1.0f});
	for (int step = 0; step < kSteps; ++step)
		world.Step();
	Sim::SaveGame save = Sim::SaveGame::Of(world, "fuzz");
	save.info.mods = {{"base", "1"}};
	save.info.details = {{"place", "seed"}};
	const std::string_view purse = "credits";
	save.AddSection("game", {AsBytes(purse).begin(), AsBytes(purse).end()});
	WriteFile(corpus / "falling.save", save.Write());
	WriteFile(
	    corpus / "empty.save",
	    Sim::SaveGame::Of(Sim::SimWorld{Fuzz::kStep, Fuzz::kSeed}, "empty").Write());
}


std::vector<std::byte> Message(Net::Wire::EngineKind kind, std::span<const std::byte> body)
{
	const auto number = static_cast<std::uint16_t>(kind);
	std::vector<std::byte> message{std::byte(number & 0xffu), std::byte(number >> 8)};
	message.insert(message.end(), body.begin(), body.end());
	return message;
}


// Each engine message's plaintext, and the client's offer as it goes on the wire.
void Messages(const fs::path &corpus)
{
	const std::string_view credential = "0123456789abcdef";
	WriteFile(
	    corpus / "hello",
	    Message(
	        Net::Wire::EngineKind::Hello,
	        Net::Wire::EncodeHello(
	            {"jettison", 1, {AsBytes(credential).begin(), AsBytes(credential).end()}})));
	WriteFile(corpus / "welcome", Message(Net::Wire::EngineKind::Welcome, AsBytes("welcome")));
	WriteFile(
	    corpus / "snapshot",
	    Message(
	        Net::Wire::EngineKind::Snapshot, AsBytes(std::string_view{"\x2a\0\0\0\0\0\0\0state", 13})));
	ENetPacket *pOffer = Net::Wire::MakeOffer(Net::Seal::Key{});
	WriteFile(corpus / "offer", std::as_bytes(std::span{pOffer->data, pOffer->dataLength}));
	enet_packet_destroy(pOffer);
}


void WriteImage(void *pContext, void *pData, int size)
{
	const auto &path = *static_cast<const fs::path *>(pContext);
	WriteFile(
	    path, std::as_bytes(
	              std::span{static_cast<const std::uint8_t *>(pData), static_cast<std::size_t>(size)}));
}


// One small image of each kind stb_image reads that stb_image_write writes.
void Images(const fs::path &corpus)
{
	std::vector<std::uint8_t> texels;
	for (int y = 0; y < kImageSide; ++y)
		for (int x = 0; x < kImageSide; ++x)
			texels.insert(
			    texels.end(),
			    {static_cast<std::uint8_t>(x * 32), static_cast<std::uint8_t>(y * 32), 128, 255});
	const auto write = [&](const char *name, auto &&encode)
	{
		const fs::path path = corpus / name;
		encode(const_cast<fs::path *>(&path));
	};
	write(
	    "rgba.png",
	    [&](fs::path *pPath)
	    {
		    stbi_write_png_to_func(
		        WriteImage, pPath, kImageSide, kImageSide, 4, texels.data(), kImageSide * 4);
	    });
	write(
	    "grey.png",
	    [&](fs::path *pPath)
	    {
		    stbi_write_png_to_func(
		        WriteImage, pPath, kImageSide * 4, kImageSide, 1, texels.data(), kImageSide * 4);
	    });
	write(
	    "rgb.bmp", [&](fs::path *pPath)
	    { stbi_write_bmp_to_func(WriteImage, pPath, kImageSide, kImageSide, 4, texels.data()); });
	write(
	    "rgba.tga", [&](fs::path *pPath)
	    { stbi_write_tga_to_func(WriteImage, pPath, kImageSide, kImageSide, 4, texels.data()); });
	write(
	    "rgb.jpg",
	    [&](fs::path *pPath)
	    {
		    stbi_write_jpg_to_func(
		        WriteImage, pPath, kImageSide, kImageSide, 4, texels.data(), kJpegQuality);
	    });
}


// The content's sounds, the head of its music, and the formats it has none of: WAV at 8 bits and
// as floats, and MP3.
void Sounds(const fs::path &repository, const fs::path &corpus)
{
	const fs::path audio = repository / "source/test/content/audio";
	Copy(audio, {".wav"}, corpus);
	std::vector<std::byte> music = ReadFile(audio / "music/drift.flac");
	music.resize(std::min(music.size(), kMusicHeadBytes));
	WriteFile(corpus / "drift-head.flac", music);
	const auto wave = [](std::uint16_t format, std::uint16_t bits, std::string_view samples)
	{
		const auto le = [](std::uint32_t value, int bytes)
		{
			std::string text;
			for (int index = 0; index < bytes; ++index)
				text.push_back(static_cast<char>((value >> (8 * index)) & 0xffu));
			return text;
		};
		const std::uint32_t rate = 8000;
		const std::string chunks = "fmt " + le(16, 4) + le(format, 2) + le(1, 2) + le(rate, 4) +
		                           le(rate * bits / 8, 4) + le(bits / 8, 2) + le(bits, 2) + "data" +
		                           le(static_cast<std::uint32_t>(samples.size()), 4) +
		                           std::string{samples};
		return "RIFF" + le(static_cast<std::uint32_t>(4 + chunks.size()), 4) + "WAVE" + chunks;
	};
	const std::string pcm8 = wave(1, 8, "\x80\xa0\xc0\xa0\x80\x60\x40\x60");
	WriteFile(corpus / "pcm8.wav", AsBytes(pcm8));
	const std::array<float, 4> floats{0.0f, 0.5f, 0.0f, -0.5f};
	const std::string ieee =
	    wave(3, 32, {reinterpret_cast<const char *>(floats.data()), sizeof(floats)});
	WriteFile(corpus / "float.wav", AsBytes(ieee));
	std::vector<std::byte> mp3;
	for (const char tagByte : std::string_view{"ID3\x04\0\0\0\0\0\0", 10})
		mp3.push_back(std::byte(tagByte));
	for (int frame = 0; frame < kMp3Frames; ++frame)
	{
		for (const std::uint8_t headerByte : kMp3FrameHeader)
			mp3.push_back(std::byte{headerByte});
		mp3.resize(mp3.size() + kMp3FrameBytes - kMp3FrameHeader.size());
	}
	WriteFile(corpus / "silence.mp3", mp3);
}
} // namespace


int main(int argc, char **argv)
{
	if (argc != 3)
	{
		std::cerr << "usage: jettison_fuzz_seeds <repository> <seeds directory>\n";
		return 2;
	}
	const fs::path repository{argv[1]};
	const fs::path seeds{argv[2]};
	try
	{
		const fs::path content = repository / "source/test/content";
		const fs::path assets = repository / "assets";
		Models(repository, seeds / "gltf");
		fs::create_directories(seeds / "pack");
		for (const char *directory : {"animations", "grades", "materials"})
			Vfs::WritePack(content / directory, seeds / "pack" / (std::string{directory} + ".pack"));
		Textures(content / "textures", seeds / "ktx2");
		Images(seeds / "image");
		Saves(seeds / "save");
		Messages(seeds / "net");
		CollisionFiles(repository, seeds / "collision");
		Copy(content / "animations", {".clip.yaml"}, seeds / "clip");
		Copy(assets / "strings", {".yaml"}, seeds / "strings");
		Sounds(repository, seeds / "audio");
		Copy(assets / "fonts", {".ttf"}, seeds / "font");
		Copy(assets / "ui", {".rml"}, seeds / "rml");
		Copy(assets / "ui", {".rcss"}, seeds / "rcss");
	}
	catch (const std::exception &error)
	{
		std::cerr << "jettison_fuzz_seeds: " << error.what() << "\n";
		return 1;
	}
	return 0;
}
