#include <doctest/doctest.h>

#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <glm/glm.hpp>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include "CookTree.h"
#include "Watch.h"
#include <jettison/geometry/GltfFile.h>
#include <jettison/physics/CollisionFile.h>

using namespace Jettison;
namespace fs = std::filesystem;

namespace
{
std::vector<std::byte> Read(const fs::path &path)
{
	std::ifstream stream{path, std::ios::binary};
	REQUIRE(stream);
	const std::vector<char> chars{
	    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(chars.size());
	std::memcpy(bytes.data(), chars.data(), chars.size());
	return bytes;
}


std::string ReadText(const fs::path &path)
{
	const auto bytes = Read(path);
	return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}


void Write(const fs::path &path, const void *pData, std::size_t size)
{
	fs::create_directories(path.parent_path());
	std::ofstream stream{path, std::ios::binary};
	stream.write(static_cast<const char *>(pData), static_cast<std::streamsize>(size));
	REQUIRE(stream.flush());
}


void WriteText(const fs::path &path, const std::string &text)
{
	Write(path, text.data(), text.size());
}


// A PNG of 8 by 8 pixels, its colours shifted by shade.
void WritePng(const fs::path &path, int shade)
{
	std::vector<unsigned char> pixels;
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			pixels.insert(
			    pixels.end(), {static_cast<unsigned char>(x * 32), static_cast<unsigned char>(y * 32),
			                   static_cast<unsigned char>(shade)});
	std::vector<unsigned char> png;
	stbi_write_png_to_func(
	    [](void *pContext, void *pData, int size)
	    {
		    auto &out = *static_cast<std::vector<unsigned char> *>(pContext);
		    const auto *pBytes = static_cast<const unsigned char *>(pData);
		    out.insert(out.end(), pBytes, pBytes + size);
	    },
	    &png, 8, 8, 3, pixels.data(), 8 * 3);
	Write(path, png.data(), png.size());
}


// A bumpy grid of 16 by 16 quads, 512 triangles the simplifier can take from, its positions, texture
// coordinates and indices in the .bin beside it, its base colour in an image below it.
void WriteTerrain(const fs::path &directory, float bump)
{
	constexpr int kQuads = 16;
	std::vector<glm::vec3> positions;
	std::vector<glm::vec2> texCoords;
	for (int y = 0; y <= kQuads; ++y)
		for (int x = 0; x <= kQuads; ++x)
		{
			const float u = static_cast<float>(x) / kQuads;
			const float v = static_cast<float>(y) / kQuads;
			positions.emplace_back(u, v, bump * std::sin(u * 7.0f) * std::cos(v * 5.0f));
			texCoords.emplace_back(u, v);
		}
	std::vector<std::uint32_t> indices;
	for (int y = 0; y < kQuads; ++y)
		for (int x = 0; x < kQuads; ++x)
		{
			const auto corner = static_cast<std::uint32_t>((y * (kQuads + 1)) + x);
			const auto above = corner + kQuads + 1;
			indices.insert(indices.end(), {corner, corner + 1, above, corner + 1, above + 1, above});
		}
	std::vector<std::byte> bin(
	    (positions.size() * sizeof(glm::vec3)) + (texCoords.size() * sizeof(glm::vec2)) +
	    (indices.size() * sizeof(std::uint32_t)));
	const std::size_t positionBytes = positions.size() * sizeof(glm::vec3);
	const std::size_t texCoordBytes = texCoords.size() * sizeof(glm::vec2);
	std::memcpy(bin.data(), positions.data(), positionBytes);
	std::memcpy(bin.data() + positionBytes, texCoords.data(), texCoordBytes);
	std::memcpy(
	    bin.data() + positionBytes + texCoordBytes, indices.data(),
	    indices.size() * sizeof(std::uint32_t));
	Write(directory / "terrain.bin", bin.data(), bin.size());
	WriteText(
	    directory / "terrain.gltf",
	    std::format(
	        R"({{"asset":{{"version":"2.0"}},"buffers":[{{"uri":"terrain.bin","byteLength":{}}}],)"
	        R"("bufferViews":[{{"buffer":0,"byteLength":{}}},{{"buffer":0,"byteOffset":{},"byteLength":{}}},)"
	        R"({{"buffer":0,"byteOffset":{},"byteLength":{}}}],)"
	        R"("accessors":[{{"bufferView":0,"componentType":5126,"count":{},"type":"VEC3"}},)"
	        R"({{"bufferView":1,"componentType":5126,"count":{},"type":"VEC2"}},)"
	        R"({{"bufferView":2,"componentType":5125,"count":{},"type":"SCALAR"}}],)"
	        R"("images":[{{"uri":"textures/terrain-color.png"}}],"textures":[{{"source":0}}],)"
	        R"("materials":[{{"pbrMetallicRoughness":{{"baseColorTexture":{{"index":0}}}}}}],)"
	        R"("meshes":[{{"primitives":[{{"attributes":{{"POSITION":0,"TEXCOORD_0":1}},"indices":2,"material":0}}]}}],)"
	        R"("nodes":[{{"mesh":0}}],"scenes":[{{"nodes":[0]}}],"scene":0}})",
	        bin.size(), positionBytes, positionBytes, texCoordBytes, positionBytes + texCoordBytes,
	        indices.size() * sizeof(std::uint32_t), positions.size(), texCoords.size(),
	        indices.size()));
}


// A source tree of each kind of source, and an output beside it.
struct Fixture
{
	explicit Fixture(const char *name)
	    : root{fs::temp_directory_path() / "jettison-cook-tests" / name}, source{root / "source"},
	      output{root / "output"}
	{
		fs::remove_all(root);
		WriteText(source / "cook.yaml", "version: 1\nlod:\n  maxLevels: 1\n");
		WriteTerrain(source / "models", 0.1f);
		WritePng(source / "models" / "textures" / "terrain-color.png", 0);
		WriteText(source / "models" / "terrain.gltf.yaml", "version: 1\nlod:\n  maxLevels: 2\n");
		WritePng(source / "textures" / "panel-color.png", 64);
		WriteText(source / "materials" / "panel.material.yaml", "version: 1\n");
		WriteText(source / "notes" / "read-me.txt", "Not cooked, copied.\n");
	}


	~Fixture()
	{
		fs::remove_all(root);
	}


	Fixture(const Fixture &) = delete;


	Fixture &operator=(const Fixture &) = delete;


	Cook::CookReport Run() const
	{
		return Cook::CookTree(source, output);
	}


	std::unique_ptr<Geometry::GltfFile> Model() const
	{
		return Geometry::GltfFile::Parse(Read(output / "models" / "terrain.glb"), "terrain.glb");
	}

	fs::path root;
	fs::path source;
	fs::path output;
};


bool IsKtx2(const std::vector<std::byte> &bytes)
{
	constexpr unsigned char kIdentifier[] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB};
	return bytes.size() > sizeof kIdentifier &&
	       std::memcmp(bytes.data(), kIdentifier, sizeof kIdentifier) == 0;
}


bool Mentions(const std::vector<std::string> &messages, const std::string &text)
{
	return std::ranges::any_of(
	    messages, [&](const std::string &message) { return message.find(text) != std::string::npos; });
}


// Whether the condition holds within a bound far past any cook of the fixture, the sanitised build's
// included.
template <typename Condition> bool Eventually(Condition condition)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
	while (std::chrono::steady_clock::now() < deadline)
	{
		if (condition())
			return true;
		std::this_thread::sleep_for(std::chrono::milliseconds{20});
	}
	return false;
}


// The cook key the model's GLB carries, or empty when there is none.
std::string ModelKey(const Fixture &fixture)
{
	const fs::path path = fixture.output / "models" / "terrain.glb";
	return fs::exists(path) ? fixture.Model()->CookKey() : std::string{};
}


// The watch on its own thread, stopped and joined as it goes.
class Watching
{
  public:
	explicit Watching(const Fixture &fixture)
	    : m_thread{[&] { Cook::WatchTree(fixture.source, fixture.output, m_stop); }}
	{
	}


	~Watching()
	{
		m_stop = true;
	}


	Watching(const Watching &) = delete;


	Watching &operator=(const Watching &) = delete;

  private:
	std::atomic<bool> m_stop{false};
	std::jthread m_thread;
};
} // namespace


TEST_CASE("The cook mirrors a tree: models with their textures and collision, loose images, copies")
{
	const Fixture fixture{"tree"};
	const Cook::CookReport report = fixture.Run();
	CHECK(report.refusals.empty());
	CHECK(report.cooked.size() == 4);

	for (const char *output :
	     {"models/terrain.glb", "collision/terrain.collision", "textures/panel-color.ktx2",
	      "materials/panel.material.yaml", "notes/read-me.txt", "cook-record.yaml"})
		CHECK_MESSAGE(fs::exists(fixture.output / output), output);
	for (const char *notOutput :
	     {"cook.yaml", "models/terrain.gltf.yaml", "models/terrain.bin",
	      "models/textures/terrain-color.png", "models/textures/terrain-color.ktx2"})
		CHECK_MESSAGE(!fs::exists(fixture.output / notOutput), notOutput);
	CHECK(
	    Read(fixture.output / "notes" / "read-me.txt") ==
	    Read(fixture.source / "notes" / "read-me.txt"));
	CHECK(IsKtx2(Read(fixture.output / "textures" / "panel-color.ktx2")));

	const auto model = fixture.Model();
	const auto &asset = model->Asset();
	CHECK(
	    std::ranges::find(asset.extensionsRequired, "KHR_texture_basisu") !=
	    asset.extensionsRequired.end());
	REQUIRE(asset.textures.size() == 1);
	CHECK(asset.textures[0].basisuImageIndex == 0u);
	CHECK_FALSE(asset.textures[0].imageIndex.has_value());
	const auto &view = std::get<fastgltf::sources::BufferView>(asset.images[0].data);
	CHECK(view.mimeType == fastgltf::MimeType::KTX2);
	const auto image = fastgltf::DefaultBufferDataAdapter{}(asset, view.bufferViewIndex);
	CHECK(IsKtx2({image.begin(), image.end()}));

	// The record names the model by the key its GLB carries.
	CHECK(!model->CookKey().empty());
	CHECK(ReadText(fixture.output / "cook-record.yaml").find(model->CookKey()) != std::string::npos);

	const auto shape = Physics::ReadCollisionFile(
	    Read(fixture.output / "collision" / "terrain.collision"), "collision/terrain.collision");
	CHECK(shape.parts.size() == 1);
}


TEST_CASE("A source's settings file goes over cook.yaml, which goes over the engine's defaults")
{
	const Fixture fixture{"layers"};
	REQUIRE(fixture.Run().refusals.empty());
	CHECK(fixture.Model()->Levels(0).size() == 2);

	fs::remove(fixture.source / "models" / "terrain.gltf.yaml");
	const Cook::CookReport report = fixture.Run();
	CHECK(report.cooked == std::vector<std::string>{"models/terrain.gltf"});
	CHECK(fixture.Model()->Levels(0).size() == 1);
}


TEST_CASE(
    "Settings with a key the cook does not know, or a value out of range, are refused, naming the file")
{
	const Fixture fixture{"bad-settings"};
	WriteText(fixture.source / "models" / "terrain.gltf.yaml", "version: 1\nlod:\n  reduction: 1.5\n");
	Cook::CookReport report = fixture.Run();
	CHECK(report.refusals.size() == 1);
	CHECK(Mentions(report.refusals, "models/terrain.gltf.yaml"));
	CHECK(!fs::exists(fixture.output / "models" / "terrain.glb"));

	WriteText(fixture.source / "cook.yaml", "version: 1\nlod:\n  maxLevelz: 2\n");
	report = fixture.Run();
	CHECK(report.refusals.size() == 2);
	CHECK(Mentions(report.refusals, "cook.yaml"));
	CHECK(Mentions(report.refusals, "maxLevelz"));
}


TEST_CASE(
    "An unchanged source is skipped, and a model is cooked again when only its buffer, image or "
    "cook.yaml changes")
{
	const Fixture fixture{"unchanged"};
	REQUIRE(fixture.Run().refusals.empty());
	const std::string key = fixture.Model()->CookKey();

	Cook::CookReport report = fixture.Run();
	CHECK(report.cooked.empty());
	CHECK(report.skipped.size() == 4);

	// The bumps move the positions alone: the .gltf is the same text.
	WriteTerrain(fixture.source / "models", 0.2f);
	report = fixture.Run();
	CHECK(report.cooked == std::vector<std::string>{"models/terrain.gltf"});
	const std::string bufferKey = fixture.Model()->CookKey();
	CHECK(bufferKey != key);

	WritePng(fixture.source / "models" / "textures" / "terrain-color.png", 128);
	report = fixture.Run();
	CHECK(report.cooked == std::vector<std::string>{"models/terrain.gltf"});
	CHECK(fixture.Model()->CookKey() != bufferKey);

	WriteText(
	    fixture.source / "cook.yaml", "version: 1\nlod:\n  maxLevels: 1\ntextures:\n  quality: 1\n");
	report = fixture.Run();
	CHECK(report.cooked == std::vector<std::string>{"models/terrain.gltf", "textures/panel-color.png"});
	CHECK(report.skipped.size() == 2);
}


TEST_CASE("An output whose source has gone is deleted, and no other file in the output is touched")
{
	const Fixture fixture{"lost"};
	REQUIRE(fixture.Run().refusals.empty());
	WriteText(fixture.output / "notes" / "mine.txt", "Not the cook's.\n");

	fs::remove(fixture.source / "notes" / "read-me.txt");
	fs::remove(fixture.source / "models" / "terrain.gltf");
	const Cook::CookReport report = fixture.Run();
	CHECK(report.refusals.empty());
	CHECK(
	    report.deleted ==
	    std::vector<std::string>{
	        "models/terrain.glb", "collision/terrain.collision", "notes/read-me.txt"});
	CHECK(!fs::exists(fixture.output / "notes" / "read-me.txt"));
	CHECK(!fs::exists(fixture.output / "models" / "terrain.glb"));
	CHECK(fs::exists(fixture.output / "notes" / "mine.txt"));

	// With no model to reference them, its buffer and image are sources of their own.
	CHECK(fs::exists(fixture.output / "models" / "terrain.bin"));
	CHECK(fs::exists(fixture.output / "models" / "textures" / "terrain-color.ktx2"));
}


TEST_CASE("The same tree cooks to the same bytes twice")
{
	const Fixture fixture{"twice"};
	REQUIRE(Cook::CookTree(fixture.source, fixture.root / "first").refusals.empty());
	REQUIRE(Cook::CookTree(fixture.source, fixture.root / "second").refusals.empty());
	std::size_t compared = 0;
	for (const auto &entry : fs::recursive_directory_iterator{fixture.root / "first"})
		if (entry.is_regular_file())
		{
			const fs::path relative = entry.path().lexically_relative(fixture.root / "first");
			CHECK_MESSAGE(
			    Read(entry.path()) == Read(fixture.root / "second" / relative), relative.string());
			++compared;
		}
	CHECK(compared == 6);
}


TEST_CASE("A source whose name breaks the rule, or a glTF the cook cannot read, is refused, naming it")
{
	const Fixture fixture{"refused"};
	WritePng(fixture.source / "textures" / "Panel_Two.png", 0);
	WriteText(fixture.source / "models" / "broken.gltf", "not a model");
	const Cook::CookReport report = fixture.Run();
	CHECK(report.refusals.size() == 2);
	CHECK(Mentions(report.refusals, "textures/Panel_Two.png"));
	CHECK(Mentions(report.refusals, "models/broken.gltf"));
	CHECK(!fs::exists(fixture.output / "textures" / "Panel_Two.ktx2"));
	CHECK(fs::exists(fixture.output / "models" / "terrain.glb"));
}


TEST_CASE("A font keeps its upstream name and is copied, though its directory follows the rule")
{
	const Fixture fixture{"font"};
	WriteText(fixture.source / "fonts" / "Roboto-Medium.ttf", "a font");
	WriteText(fixture.source / "Fonts" / "inter.otf", "a font");
	const Cook::CookReport report = fixture.Run();
	CHECK(report.refusals.size() == 1);
	CHECK(Mentions(report.refusals, "Fonts/inter.otf"));
	CHECK(
	    Read(fixture.output / "fonts" / "Roboto-Medium.ttf") ==
	    Read(fixture.source / "fonts" / "Roboto-Medium.ttf"));
}


TEST_CASE("The watch follows sources edited, added, renamed and deleted, and a buffer edited alone")
{
	const Fixture fixture{"watch"};
	const fs::path notes = fixture.source / "notes";
	const fs::path cooked = fixture.output / "notes";
	const Watching watching{fixture};
	REQUIRE(Eventually([&] { return fs::exists(fixture.output / "cook-record.yaml"); }));
	const std::string key = ModelKey(fixture);
	REQUIRE(!key.empty());

	WriteText(notes / "read-me.txt", "Edited.\n");
	CHECK(Eventually([&] { return ReadText(cooked / "read-me.txt") == "Edited.\n"; }));

	WriteText(notes / "added.txt", "Added.\n");
	CHECK(Eventually([&] { return fs::exists(cooked / "added.txt"); }));

	// An editor's save: written to a temporary name, then renamed over the source.
	WriteText(notes / "read-me.txt.tmp", "Saved.\n");
	fs::rename(notes / "read-me.txt.tmp", notes / "read-me.txt");
	CHECK(Eventually([&] { return ReadText(cooked / "read-me.txt") == "Saved.\n"; }));

	fs::rename(notes / "added.txt", notes / "renamed.txt");
	CHECK(Eventually(
	    [&] { return fs::exists(cooked / "renamed.txt") && !fs::exists(cooked / "added.txt"); }));

	fs::remove(notes / "renamed.txt");
	CHECK(Eventually([&] { return !fs::exists(cooked / "renamed.txt"); }));

	// A new directory is watched: a file written into it later is cooked too.
	fs::create_directories(fixture.source / "more");
	WriteText(fixture.source / "more" / "later.txt", "Later.\n");
	CHECK(Eventually([&] { return fs::exists(fixture.output / "more" / "later.txt"); }));

	// The buffer alone: the .gltf is not written.
	WriteTerrain(fixture.root / "staging", 0.3f);
	fs::copy_file(
	    fixture.root / "staging" / "terrain.bin", fixture.source / "models" / "terrain.bin",
	    fs::copy_options::overwrite_existing);
	CHECK(Eventually([&] { return ModelKey(fixture) != key; }));
}


TEST_CASE("The watch reports a source it refuses, keeps its outputs and goes on")
{
	const Fixture fixture{"watch-refused"};
	const Watching watching{fixture};
	REQUIRE(Eventually([&] { return fs::exists(fixture.output / "cook-record.yaml"); }));
	const std::string key = ModelKey(fixture);
	REQUIRE(!key.empty());

	// Half written, as an exporter leaves it mid-save, with another source changed beside it so the
	// cook that took it is known to be done.
	WriteText(fixture.source / "models" / "terrain.gltf", "{\"asset\":");
	WriteText(fixture.source / "notes" / "read-me.txt", "Edited.\n");
	REQUIRE(
	    Eventually([&] { return ReadText(fixture.output / "notes" / "read-me.txt") == "Edited.\n"; }));
	CHECK(ModelKey(fixture) == key);
	CHECK(fs::exists(fixture.output / "collision" / "terrain.collision"));

	WriteTerrain(fixture.source / "models", 0.3f);
	CHECK(Eventually([&] { return !ModelKey(fixture).empty() && ModelKey(fixture) != key; }));
}


TEST_CASE(
    "An output inside the source, or holding it, is refused naming both, before anything is cooked")
{
	const Fixture fixture{"overlap"};
	const std::atomic<bool> stop{true};
	for (const fs::path &output :
	     {fixture.source, fixture.source / "cooked", fixture.source / "models" / "", fixture.root})
	{
		CAPTURE(output);
		const std::string source = fs::weakly_canonical(fixture.source).string();
		std::string cooked = fs::weakly_canonical(output).string();
		if (cooked.ends_with('/'))
			cooked.pop_back();
		CHECK_THROWS_WITH_AS(
		    Cook::CookTree(fixture.source, output), doctest::Contains(cooked), std::runtime_error);
		CHECK_THROWS_WITH_AS(
		    Cook::CookTree(fixture.source, output), doctest::Contains(source), std::runtime_error);
		CHECK_THROWS_WITH_AS(
		    Cook::WatchTree(fixture.source, output, stop), doctest::Contains(source),
		    std::runtime_error);
		CHECK(!fs::exists(output / "cook-record.yaml"));
	}
	CHECK(!fs::exists(fixture.source / "cooked"));

	// Beside it, with a name that starts as the source's does, is no overlap.
	CHECK(Cook::CookTree(fixture.source, fixture.root / "source-cooked").refusals.empty());
}
