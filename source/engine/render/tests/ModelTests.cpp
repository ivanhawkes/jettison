#include "vulkan/Model.h"

#include <glm/gtc/matrix_transform.hpp>

#include <jettison/animation/ModelFile.h>
#include <jettison/animation/RigLoader.h>
#include <jettison/geometry/Cook.h>
#include <jettison/geometry/GltfFile.h>
#include <jettison/jobs/IoWorker.h>
#include <jettison/render/Culling.h>
#include <jettison/resource/ResourceManager.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
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
std::vector<std::byte> ReadFile(const std::filesystem::path &path)
{
	std::ifstream stream{path, std::ios::binary};
	REQUIRE(stream);
	const std::vector<char> chars{
	    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(chars.size());
	std::memcpy(bytes.data(), chars.data(), chars.size());
	return bytes;
}


std::vector<std::byte> ReadContent(const std::filesystem::path &relative)
{
	return ReadFile(std::filesystem::path{JETTISON_TEST_CONTENT_DIR} / relative);
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


TEST_CASE("Model: a joint of no weight is pointed at the skin's first, since the shader reads it")
{
	// A triangle skinned to one joint, its first corner naming joint 9 at no weight.
	const std::string text = R"({"asset": {"version": "2.0"}, "nodes": [{"mesh": 0, "skin": 0}],
		"skins": [{"joints": [0]}],
		"buffers": [{"byteLength": 96, "uri": "data:application/octet-stream;base64,)"
	                         "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAkAAAAAAAAAAAAAAACAPwAAA"
	                         "AAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAA"
	                         R"("}],
		"bufferViews": [{"buffer": 0, "byteLength": 96}],
		"accessors": [
			{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
			{"bufferView": 0, "byteOffset": 36, "componentType": 5121, "count": 3, "type": "VEC4"},
			{"bufferView": 0, "byteOffset": 48, "componentType": 5126, "count": 3, "type": "VEC4"}],
		"meshes": [{"primitives": [{"attributes": {"POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2}}]}]})";
	std::vector<std::byte> bytes(text.size());
	std::memcpy(bytes.data(), text.data(), text.size());
	const auto model = Model::FromGltf(*Geometry::GltfFile::Parse(bytes, "skinned.gltf"));
	REQUIRE(model->Vertices().size() == 3);
	for (const Vertex &vertex : model->Vertices())
		CHECK(vertex.joints == glm::uvec4{0});
}


TEST_CASE("Model: the generated asteroid at the reference views' distances, each at its level")
{
	const auto file = Geometry::GltfFile::Parse(
	    ReadFile(
	        std::filesystem::path{JETTISON_COOKED_CONTENT_DIR} / "models/asteroids/asteroid-large.glb"),
	    "asteroid-large.glb");
	const auto model = Model::FromGltf(*file);
	REQUIRE(model->m_lods.size() == 5);

	// The views' camera (scene/asteroid.yaml) in capture.sh's 960 x 540 frame, at the default
	// threshold; each distance is the orbit's, from the asteroid's centre.
	CameraView camera;
	camera.verticalFov = glm::radians(45.0f);
	camera.nearPlane = 0.1f;
	const LodSelection selection = MakeLodSelection(camera, 540.0, double{kDefaultLodThreshold});
	const auto at = [&](double distance)
	{
		return TransformBounds(model->m_bounds, glm::translate(glm::dmat4{1.0}, {0.0, 0.0, -distance}));
	};
	const auto choose = [&](double distance, std::optional<std::uint32_t> previous)
	{ return SelectLod(model->m_lods, 1.0, at(distance), selection, previous); };

	// source/test/frames/views.txt: asteroid-near, asteroid-mid and asteroid-far.
	CHECK(choose(88.0, std::nullopt) == 1u);
	CHECK(choose(165.0, std::nullopt) == 3u);
	CHECK(choose(400.0, std::nullopt) == 4u);

	// Flown in from the far view to past the near one and out again a percent a frame, the level
	// changes a step at a time, once each way at each switch, and further off going out than in.
	std::vector<std::uint32_t> levels;
	std::vector<double> inward;
	std::vector<double> outward;
	std::uint32_t level = choose(400.0, std::nullopt);
	double distance = 400.0;
	for (; distance > 80.0; distance *= 0.99)
	{
		const std::uint32_t next = choose(distance, level);
		if (next != level)
		{
			CHECK(next + 1 == level);
			inward.push_back(distance);
		}
		level = next;
	}
	CHECK(level == 1u);
	for (; distance < 400.0; distance /= 0.99)
	{
		const std::uint32_t next = choose(distance, level);
		if (next != level)
		{
			CHECK(next == level + 1);
			outward.push_back(distance);
		}
		level = next;
	}
	CHECK(level == 4u);
	REQUIRE(inward.size() == 3);
	REQUIRE(outward.size() == 3);
	for (std::size_t step = 0; step < 3; ++step)
		CHECK(outward[2 - step] > inward[step]);

	// Held at a switch's distance, swaying two percent either way, the level stays where it is.
	for (const double held : inward)
	{
		CAPTURE(held);
		const std::uint32_t start = choose(held, std::nullopt);
		std::uint32_t swaying = start;
		for (int frame = 0; frame < 20; ++frame)
			swaying = choose(held * (frame % 2 == 0 ? 1.02 : 0.98), swaying);
		CHECK(swaying == start);
	}
}


TEST_CASE("Model: a file's missing normals are its faces' summed, as the cook makes them")
{
	// A tetrahedron with no normals, each corner shared by three faces at angles.
	const std::string text = R"({"asset": {"version": "2.0"}, "nodes": [{"mesh": 0}],
		"scenes": [{"nodes": [0]}], "scene": 0,
		"buffers": [{"byteLength": 72, "uri": "data:application/octet-stream;base64,)"
	                         "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAA"
	                         "AIA/AAACAAEAAAABAAMAAAADAAIAAQACAAMA"
	                         R"("}],
		"bufferViews": [{"buffer": 0, "byteLength": 48}, {"buffer": 0, "byteOffset": 48, "byteLength": 24}],
		"accessors": [
			{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 1]},
			{"bufferView": 1, "componentType": 5123, "count": 12, "type": "SCALAR"}],
		"meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}]})";
	std::vector<std::byte> bytes(text.size());
	std::memcpy(bytes.data(), text.data(), text.size());
	const auto source = Geometry::GltfFile::Parse(bytes, "tetrahedron.gltf");
	const auto uncooked = Model::FromGltf(*source);
	const auto cooked = Model::FromGltf(
	    *Geometry::GltfFile::Parse(Geometry::CookModel(*source, {}, "key"), "tetrahedron.glb"));
	REQUIRE(uncooked->Vertices().size() == 4);
	REQUIRE(cooked->Vertices().size() == 4);

	// The origin's three faces sum to the direction away from the other corners.
	const glm::vec3 origin = glm::normalize(glm::vec3{-1.0f});
	for (const Vertex &vertex : uncooked->Vertices())
	{
		CAPTURE(vertex.pos);
		if (vertex.pos == glm::vec3{0.0f})
			CHECK(glm::length(vertex.normal - origin) < 1e-6f);

		// The cook keeps positions as floats and stores normals in 16 bits.
		const auto match = std::ranges::find(cooked->Vertices(), vertex.pos, &Vertex::pos);
		REQUIRE(match != cooked->Vertices().end());
		CHECK(glm::length(match->normal - vertex.normal) < 1e-4f);
	}
}


TEST_CASE("Model: primitives sharing their vertices have normals made across them, cooked or not")
{
	// The tetrahedron split by material in two, its faces two each, over one POSITION accessor, so
	// each corner's faces lie in both.
	const std::string text = R"({"asset": {"version": "2.0"}, "nodes": [{"mesh": 0}],
		"scenes": [{"nodes": [0]}], "scene": 0, "materials": [{}, {}],
		"buffers": [{"byteLength": 72, "uri": "data:application/octet-stream;base64,)"
	                         "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAA"
	                         "AIA/AAACAAEAAAABAAMAAAADAAIAAQACAAMA"
	                         R"("}],
		"bufferViews": [{"buffer": 0, "byteLength": 48},
			{"buffer": 0, "byteOffset": 48, "byteLength": 12},
			{"buffer": 0, "byteOffset": 60, "byteLength": 12}],
		"accessors": [
			{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
				"min": [0, 0, 0], "max": [1, 1, 1]},
			{"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"},
			{"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"}],
		"meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1, "material": 0},
			{"attributes": {"POSITION": 0}, "indices": 2, "material": 1}]}]})";
	std::vector<std::byte> bytes(text.size());
	std::memcpy(bytes.data(), text.data(), text.size());
	const auto source = Geometry::GltfFile::Parse(bytes, "split.gltf");
	const auto uncooked = Model::FromGltf(*source);
	const auto cooked = Model::FromGltf(
	    *Geometry::GltfFile::Parse(Geometry::CookModel(*source, {}, "key"), "split.glb"));
	REQUIRE(uncooked->Vertices().size() == 4);

	// The origin's normal is its three faces', two in one primitive and one in the other.
	const auto origin = std::ranges::find(uncooked->Vertices(), glm::vec3{0.0f}, &Vertex::pos);
	REQUIRE(origin != uncooked->Vertices().end());
	CHECK(glm::length(origin->normal - glm::normalize(glm::vec3{-1.0f})) < 1e-6f);

	// The cook keeps each primitive's vertices apart, and stores normals in 16 bits.
	REQUIRE(cooked->Vertices().size() == 8);
	for (const Vertex &vertex : cooked->Vertices())
	{
		CAPTURE(vertex.pos);
		const auto match = std::ranges::find(uncooked->Vertices(), vertex.pos, &Vertex::pos);
		REQUIRE(match != uncooked->Vertices().end());
		CHECK(glm::length(match->normal - vertex.normal) < 1e-4f);
	}
}
