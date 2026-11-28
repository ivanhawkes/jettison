#include "vulkan/Model.h"

#include <jettison/animation/ModelFile.h>
#include <jettison/animation/RigLoader.h>
#include <jettison/geometry/Cook.h>
#include <jettison/geometry/GltfFile.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace Jettison;
using namespace Jettison::Render;

namespace
{
std::vector<std::byte> ReadContent(const std::filesystem::path &relative)
{
	std::ifstream stream{std::filesystem::path{JETTISON_TEST_CONTENT_DIR} / relative, std::ios::binary};
	REQUIRE(stream);
	const std::vector<char> chars{
	    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(chars.size());
	std::memcpy(bytes.data(), chars.data(), chars.size());
	return bytes;
}


void AppendWord(std::vector<std::byte> &bytes, std::uint32_t word)
{
	for (int shift = 0; shift < 32; shift += 8)
		bytes.push_back(static_cast<std::byte>((word >> shift) & 0xffu));
}


// A GLB of one triangle whose base colour is a KTX 2 image in its binary chunk, named as a cooked
// file names one: through KHR_texture_basisu, with no fallback image.
std::vector<std::byte> MakeKtx2Triangle(const std::vector<std::byte> &image)
{
	const std::vector<float> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
	const std::size_t positionBytes = positions.size() * sizeof(float);
	std::vector<std::byte> binary(positionBytes);
	std::memcpy(binary.data(), positions.data(), positionBytes);
	binary.insert(binary.end(), image.begin(), image.end());
	std::string json =
	    R"({"asset":{"version":"2.0"},"extensionsUsed":["KHR_texture_basisu"],)"
	    R"("extensionsRequired":["KHR_texture_basisu"],"buffers":[{"byteLength":)" +
	    std::to_string(binary.size()) +
	    R"(}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":)" +
	    std::to_string(image.size()) +
	    R"(}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",)"
	    R"("min":[0,0,0],"max":[1,1,0]}],"images":[{"bufferView":1,"mimeType":"image/ktx2"}],)"
	    R"("textures":[{"extensions":{"KHR_texture_basisu":{"source":0}}}],)"
	    R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}}],)"
	    R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],)"
	    R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
	json.resize((json.size() + 3) / 4 * 4, ' ');
	binary.resize((binary.size() + 3) / 4 * 4);

	std::vector<std::byte> glb;
	AppendWord(glb, 0x46546c67); // "glTF"
	AppendWord(glb, 2);
	AppendWord(glb, static_cast<std::uint32_t>(12 + 8 + json.size() + 8 + binary.size()));
	AppendWord(glb, static_cast<std::uint32_t>(json.size()));
	AppendWord(glb, 0x4e4f534a); // "JSON"
	for (const char ch : json)
		glb.push_back(static_cast<std::byte>(ch));
	AppendWord(glb, static_cast<std::uint32_t>(binary.size()));
	AppendWord(glb, 0x004e4942); // "BIN"
	glb.insert(glb.end(), binary.begin(), binary.end());
	return glb;
}
} // namespace

TEST_CASE("Model: a model file is read once for its rig and its geometry, and once again a change")
{
	const std::filesystem::path directory =
	    std::filesystem::temp_directory_path() / ("jettison-model-tests-" + std::to_string(getpid()));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory / "models");
	for (const char *name : {"arm.glb", "crate.glb"})
		std::filesystem::copy_file(
		    std::filesystem::path{JETTISON_TEST_CONTENT_DIR} / "models" / name,
		    directory / "models" / name);

	{
		Vfs::FileSystem files;
		files.AddMount({"base", directory, false});
		Jobs::IoWorker worker;
		Resource::ResourceManager manager{
		    files, worker, {.hotReload = true, .pollInterval = {}, .settleLoads = true}};
		Animation::RegisterRigLoader(manager);

		// The loader the renderer registers, less its GPU finish and retire.
		manager.RegisterLoader(
		    Resource::DerivedLoader<Model, Animation::ModelFile>{
		        [](const Animation::ModelFile &file, std::string_view)
		        { return Model::FromGltf(file); },
		        {},
		        {}});

		// The arm has a skin and a clip; the crate has neither, and its rig is its one mesh node.
		for (const std::string path : {"models/arm.glb", "models/crate.glb"})
		{
			CAPTURE(path);
			const auto rig = manager.Load<Animation::Rig>(path);
			const auto model = manager.Load<Model>(path);

			// The parse's own slot, which both hold: its revision counts the file's reads.
			const auto parse = manager.Load<Animation::ModelFile>(path);
			manager.Update();
			REQUIRE(manager.TryGet(rig) != nullptr);
			REQUIRE(manager.TryGet(model) != nullptr);
			CHECK_FALSE(manager.TryGet(model)->m_primitives.empty());
			CHECK(manager.TryGet(rig)->HasSkin() == (path == "models/arm.glb"));
			CHECK(manager.RevisionOf(parse) == 1);

			// Not kept once the rig and the geometry have it.
			CHECK(manager.StatusOf(parse) == Resource::Status::Ready);
			CHECK(manager.TryGet(parse) == nullptr);

			// A change on the disk (J14's hot reload) makes both again from one more read.
			const auto file = directory / path;
			std::filesystem::last_write_time(
			    file, std::filesystem::last_write_time(file) + std::chrono::seconds{1});
			manager.Update();
			manager.Update();
			CHECK(manager.RevisionOf(parse) == 2);
			CHECK(manager.RevisionOf(rig) == 2);
			CHECK(manager.RevisionOf(model) == 2);
			CHECK(manager.TryGet(rig) != nullptr);
			CHECK(manager.TryGet(model) != nullptr);
			manager.Release(parse);
			manager.Release(model);
			manager.Release(rig);
		}

		// Geometry alone, as a model the scene has no rig for: one read too.
		const auto model = manager.Load<Model>("models/crate.glb");
		const auto parse = manager.Load<Animation::ModelFile>("models/crate.glb");
		manager.Update();
		CHECK(manager.TryGet(model) != nullptr);
		CHECK(manager.RevisionOf(parse) == 1);
		manager.Release(parse);
		manager.Release(model);
	}
	std::error_code error;
	std::filesystem::remove_all(directory, error);
}


TEST_CASE("Model: a cooked file's levels of detail, and its materials from the finest level")
{
	for (const char *name : {"arm.glb", "crate.glb"})
	{
		CAPTURE(name);
		const auto bytes = ReadContent(std::filesystem::path{"models"} / name);
		const auto source = Geometry::GltfFile::Parse(bytes, name);
		const auto cooked = Geometry::GltfFile::Parse(Geometry::CookModel(*source, {}, "key"), name);
		const auto uncooked = Model::FromGltf(*source);
		const auto model = Model::FromGltf(*cooked);

		// A file that was not cooked is one level, exact.
		REQUIRE(uncooked->m_lods.size() == 1);
		CHECK(uncooked->m_lods[0].primitiveCount == uncooked->m_primitives.size());
		CHECK(uncooked->m_lods[0].error == 0.0f);

		const auto levels = cooked->Levels(0);
		REQUIRE(model->m_lods.size() == levels.size());
		for (std::size_t level = 0; level < levels.size(); ++level)
		{
			CHECK(model->m_lods[level].firstPrimitive == levels[level].firstPrimitive);
			CHECK(model->m_lods[level].primitiveCount == levels[level].primitiveCount);
			CHECK(model->m_lods[level].error == levels[level].error);
		}
		CHECK(model->m_lods[0].primitiveCount == uncooked->m_primitives.size());
		for (std::uint32_t primitive = 0; primitive < model->m_primitives.size(); ++primitive)
		{
			const std::uint32_t material = model->MaterialPrimitive(primitive);
			CHECK(
			    model->m_primitives[primitive].baseColorImageData.empty() ==
			    (primitive != material || uncooked->m_primitives[material].baseColorImageData.empty()));
		}

		// Positions are kept as floats, so the bounds are the same.
		CHECK(model->m_bounds.center.x == doctest::Approx(uncooked->m_bounds.center.x));
		CHECK(model->m_bounds.center.y == doctest::Approx(uncooked->m_bounds.center.y));
		CHECK(model->m_bounds.center.z == doctest::Approx(uncooked->m_bounds.center.z));
		CHECK(model->m_bounds.radius == doctest::Approx(uncooked->m_bounds.radius));
	}
}


TEST_CASE("Model: a base colour through KHR_texture_basisu decodes as KTX 2, a PNG through stb_image")
{
	const auto image = ReadContent("textures/panels/panels-emissive.ktx2");
	const auto file = Geometry::GltfFile::Parse(MakeKtx2Triangle(image), "ktx.glb");
	const auto model = Model::FromGltf(*file);
	REQUIRE(model->m_primitives.size() == 1);
	const ModelPrimitive &primitive = model->m_primitives[0];
	CHECK(primitive.baseColorKtx2);
	CHECK(primitive.baseColorImageData == image);
	const TextureData decoded = primitive.DecodeBaseColor(TranscodeTarget::Rgba8, "ktx.glb-p0");
	const TextureData expected = DecodeKtx2(image, TranscodeTarget::Rgba8, "panels-emissive.ktx2");
	CHECK(decoded.format == expected.format);
	CHECK(decoded.width == expected.width);
	CHECK(decoded.height == expected.height);
	CHECK(decoded.bytes == expected.bytes);

	const auto arm =
	    Model::FromGltf(*Geometry::GltfFile::Parse(ReadContent("models/arm.glb"), "arm.glb"));
	REQUIRE_FALSE(arm->m_primitives[0].baseColorImageData.empty());
	CHECK_FALSE(arm->m_primitives[0].baseColorKtx2);
	CHECK(
	    arm->m_primitives[0].DecodeBaseColor(TranscodeTarget::Rgba8, "arm.glb-p0").format ==
	    TextureFormat::Rgba8Srgb);
}
