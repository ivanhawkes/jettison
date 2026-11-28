#include <doctest/doctest.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/glm.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

#include <jettison/geometry/Cook.h>
#include <jettison/geometry/GltfFile.h>

using namespace Jettison;

namespace
{
std::vector<std::byte> ReadContent(const char *relative)
{
	std::ifstream stream{std::filesystem::path{JETTISON_TEST_CONTENT_DIR} / relative, std::ios::binary};
	REQUIRE(stream);
	const std::vector<char> chars{
	    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	std::vector<std::byte> bytes(chars.size());
	std::memcpy(bytes.data(), chars.data(), chars.size());
	return bytes;
}


template <typename T> void Append(std::vector<std::byte> &bytes, const std::vector<T> &values)
{
	const auto *pBytes = reinterpret_cast<const std::byte *>(values.data());
	bytes.insert(bytes.end(), pBytes, pBytes + (values.size() * sizeof(T)));
}


// A rock: an icosphere subdivided four times (5120 triangles), its radius displaced so the simplifier
// has detail to take away, with texture coordinates and no normals, so the cook makes both.
std::vector<std::byte> MakeRock(bool withScene = true)
{
	const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
	std::vector<glm::vec3> positions{{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
	                                 {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
	                                 {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
	std::vector<std::uint32_t> indices{0, 11, 5, 0, 5,  1,  0,  1,  7,  0,  7, 10, 0, 10, 11,
	                                   1, 5,  9, 5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1,  8,
	                                   3, 9,  4, 3, 4,  2,  3,  2,  6,  3,  6, 8,  3, 8,  9,
	                                   4, 9,  5, 2, 4,  11, 6,  2,  10, 8,  6, 7,  9, 8,  1};
	for (int subdivision = 0; subdivision < 4; ++subdivision)
	{
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> midpoints;
		const auto midpoint = [&](std::uint32_t a, std::uint32_t b)
		{
			const auto key = std::minmax(a, b);
			const auto [found, inserted] =
			    midpoints.try_emplace(key, static_cast<std::uint32_t>(positions.size()));
			if (inserted)
				positions.push_back((positions[a] + positions[b]) * 0.5f);
			return found->second;
		};
		std::vector<std::uint32_t> finer;
		for (std::size_t index = 0; index < indices.size(); index += 3)
		{
			const std::uint32_t a = indices[index], b = indices[index + 1], c = indices[index + 2];
			const std::uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
			finer.insert(finer.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
		}
		indices = std::move(finer);
	}
	std::vector<glm::vec2> texCoords;
	for (glm::vec3 &position : positions)
	{
		const glm::vec3 direction = glm::normalize(position);
		position = direction * (1.0f + 0.1f * std::sin(5.0f * direction.x) *
		                                   std::sin(7.0f * direction.y) * std::sin(3.0f * direction.z));
		texCoords.emplace_back(
		    0.5f + std::atan2(direction.z, direction.x) / 6.2831853f,
		    std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / 3.1415927f);
	}

	fastgltf::Asset asset;
	std::vector<std::byte> bytes;
	Append(bytes, positions);
	Append(bytes, texCoords);
	Append(bytes, indices);
	const std::size_t positionBytes = positions.size() * sizeof(glm::vec3);
	const std::size_t texCoordBytes = texCoords.size() * sizeof(glm::vec2);
	fastgltf::Buffer buffer{};
	buffer.byteLength = bytes.size();
	buffer.data = fastgltf::sources::Vector{std::move(bytes)};
	asset.buffers.push_back(std::move(buffer));
	const auto view = [&](std::size_t offset, std::size_t length)
	{
		fastgltf::BufferView bufferView{};
		bufferView.bufferIndex = 0;
		bufferView.byteOffset = offset;
		bufferView.byteLength = length;
		asset.bufferViews.push_back(std::move(bufferView));
	};
	view(0, positionBytes);
	view(positionBytes, texCoordBytes);
	view(positionBytes + texCoordBytes, indices.size() * sizeof(std::uint32_t));
	const auto accessor = [&](std::size_t bufferView, std::size_t count, fastgltf::AccessorType type,
	                          fastgltf::ComponentType componentType)
	{
		fastgltf::Accessor made{};
		made.bufferViewIndex = bufferView;
		made.count = count;
		made.type = type;
		made.componentType = componentType;
		asset.accessors.push_back(std::move(made));
	};
	accessor(0, positions.size(), fastgltf::AccessorType::Vec3, fastgltf::ComponentType::Float);
	accessor(1, texCoords.size(), fastgltf::AccessorType::Vec2, fastgltf::ComponentType::Float);
	accessor(2, indices.size(), fastgltf::AccessorType::Scalar, fastgltf::ComponentType::UnsignedInt);
	auto min = fastgltf::AccessorBoundsArray::ForType<double>(3);
	auto max = fastgltf::AccessorBoundsArray::ForType<double>(3);
	for (int axis = 0; axis < 3; ++axis)
	{
		min.data<double>()[axis] = 1e9;
		max.data<double>()[axis] = -1e9;
		for (const glm::vec3 &position : positions)
		{
			min.data<double>()[axis] = std::min(min.data<double>()[axis], double{position[axis]});
			max.data<double>()[axis] = std::max(max.data<double>()[axis], double{position[axis]});
		}
	}
	asset.accessors[0].min = std::move(min);
	asset.accessors[0].max = std::move(max);

	fastgltf::Primitive primitive{};
	primitive.attributes = {{"POSITION", 0}, {"TEXCOORD_0", 1}};
	primitive.indicesAccessor = 2;
	fastgltf::Mesh mesh{};
	mesh.name = "rock";
	mesh.primitives.push_back(std::move(primitive));
	asset.meshes.push_back(std::move(mesh));
	fastgltf::Node node{};
	node.name = "rock";
	node.meshIndex = 0;
	asset.nodes.push_back(std::move(node));
	if (withScene)
	{
		asset.scenes.push_back({{0}, "scene"});
		asset.defaultScene = 0;
	}

	fastgltf::Exporter exporter;
	auto written = exporter.writeGltfBinary(asset);
	REQUIRE(written.error() == fastgltf::Error::None);
	return std::move(written.get().output);
}


std::unique_ptr<Geometry::GltfFile> Cook(
    const std::vector<std::byte> &bytes, const char *name, const Geometry::LodSettings &settings = {})
{
	const auto source = Geometry::GltfFile::Parse(bytes, name);
	const auto cooked = Geometry::CookModel(*source, settings, "key-1");
	return Geometry::GltfFile::Parse(cooked, name);
}


std::size_t Triangles(
    const fastgltf::Asset &asset, const fastgltf::Mesh &mesh, const Geometry::MeshLevel &level)
{
	std::size_t count = 0;
	for (std::uint32_t index = 0; index < level.primitiveCount; ++index)
		count +=
		    asset.accessors[*mesh.primitives[level.firstPrimitive + index].indicesAccessor].count / 3;
	return count;
}


std::vector<glm::vec3> Positions(const fastgltf::Asset &asset, const fastgltf::Primitive &primitive)
{
	const auto &accessor = asset.accessors[primitive.findAttribute("POSITION")->accessorIndex];
	std::vector<glm::vec3> positions(accessor.count);
	fastgltf::iterateAccessorWithIndex<glm::vec3>(
	    asset, accessor, [&](glm::vec3 position, std::size_t index) { positions[index] = position; });
	return positions;
}


std::pair<glm::vec3, glm::vec3> Extent(const std::vector<glm::vec3> &positions)
{
	glm::vec3 low{1e9f}, high{-1e9f};
	for (const glm::vec3 &position : positions)
	{
		low = glm::min(low, position);
		high = glm::max(high, position);
	}
	return {low, high};
}
} // namespace


TEST_CASE("A cooked mesh's levels have fewer triangles and errors that grow")
{
	const auto file = Cook(MakeRock(), "rock.glb");
	const auto &asset = file->Asset();
	const auto levels = file->Levels(0);
	REQUIRE(levels.size() >= 3);
	CHECK(levels.front().error == 0.0f);
	CHECK(Triangles(asset, asset.meshes[0], levels.front()) == 5120);
	for (std::size_t index = 1; index < levels.size(); ++index)
	{
		CHECK(levels[index].error > levels[index - 1].error);
		CHECK(
		    Triangles(asset, asset.meshes[0], levels[index]) <
		    Triangles(asset, asset.meshes[0], levels[index - 1]));
	}

	// The default is five percent of the extent, about 0.1 m on a rock 2 m across.
	CHECK(levels.back().error < 0.11f);
	CHECK(file->CookKey() == "key-1");
}


TEST_CASE("The cook gives the same bytes twice")
{
	const auto bytes = MakeRock();
	const auto source = Geometry::GltfFile::Parse(bytes, "rock.glb");
	CHECK(Geometry::CookModel(*source, {}, "key") == Geometry::CookModel(*source, {}, "key"));
	CHECK(Geometry::CookModel(*source, {}, "key") != Geometry::CookModel(*source, {}, "another key"));
}


TEST_CASE("A cooked mesh keeps its finest surface, and gains normals and tangents")
{
	const auto bytes = MakeRock();
	const auto source = Geometry::GltfFile::Parse(bytes, "rock.glb");
	const auto file = Cook(bytes, "rock.glb");
	const auto &cooked = file->Asset().meshes[0].primitives[0];
	const auto [sourceLow, sourceHigh] =
	    Extent(Positions(source->Asset(), source->Asset().meshes[0].primitives[0]));
	const auto [low, high] = Extent(Positions(file->Asset(), cooked));
	CHECK(low == sourceLow);
	CHECK(high == sourceHigh);
	CHECK(cooked.findAttribute("NORMAL") != cooked.attributes.end());
	CHECK(cooked.findAttribute("TANGENT") != cooked.attributes.end());

	// Normals are stored in 16 bits and read back as unit vectors.
	const auto &normals = file->Asset().accessors[cooked.findAttribute("NORMAL")->accessorIndex];
	CHECK(normals.componentType == fastgltf::ComponentType::Short);
	fastgltf::iterateAccessor<glm::vec3>(
	    file->Asset(), normals,
	    [](glm::vec3 normal) { CHECK(glm::length(normal) == doctest::Approx(1.0f).epsilon(0.001)); });
}


TEST_CASE("A cooked model keeps its rig, clips and image")
{
	const auto bytes = ReadContent("models/arm.glb");
	const auto source = Geometry::GltfFile::Parse(bytes, "arm.glb");
	const auto file = Cook(bytes, "arm.glb");
	const auto &before = source->Asset();
	const auto &after = file->Asset();

	REQUIRE(after.nodes.size() == before.nodes.size());
	for (std::size_t index = 0; index < before.nodes.size(); ++index)
		CHECK(after.nodes[index].name == before.nodes[index].name);
	REQUIRE(after.skins.size() == 1);
	CHECK(after.skins[0].joints == before.skins[0].joints);
	const auto matrices = [](const fastgltf::Asset &asset)
	{
		std::vector<glm::mat4> read;
		fastgltf::iterateAccessor<glm::mat4>(
		    asset, asset.accessors[*asset.skins[0].inverseBindMatrices],
		    [&](const glm::mat4 &matrix) { read.push_back(matrix); });
		return read;
	};
	CHECK(matrices(after) == matrices(before));

	REQUIRE(after.animations.size() == 1);
	REQUIRE(after.animations[0].samplers.size() == before.animations[0].samplers.size());
	for (std::size_t index = 0; index < before.animations[0].samplers.size(); ++index)
	{
		const auto &beforeSampler = before.animations[0].samplers[index];
		const auto &afterSampler = after.animations[0].samplers[index];
		CHECK(afterSampler.interpolation == beforeSampler.interpolation);
		const auto values = [](const fastgltf::Asset &asset, std::size_t accessor)
		{
			std::vector<glm::vec4> read;
			const auto &readAccessor = asset.accessors[accessor];
			if (readAccessor.type == fastgltf::AccessorType::Vec4)
				fastgltf::iterateAccessor<glm::vec4>(
				    asset, readAccessor, [&](glm::vec4 value) { read.push_back(value); });
			else if (readAccessor.type == fastgltf::AccessorType::Vec3)
				fastgltf::iterateAccessor<glm::vec3>(
				    asset, readAccessor, [&](glm::vec3 value) { read.emplace_back(value, 0.0f); });
			else
				fastgltf::iterateAccessor<float>(
				    asset, readAccessor, [&](float value) { read.emplace_back(value); });
			return read;
		};
		CHECK(values(after, afterSampler.inputAccessor) == values(before, beforeSampler.inputAccessor));
		CHECK(
		    values(after, afterSampler.outputAccessor) == values(before, beforeSampler.outputAccessor));
	}

	const auto &primitive = after.meshes[0].primitives[0];
	CHECK(primitive.findAttribute("JOINTS_0") != primitive.attributes.end());
	CHECK(primitive.findAttribute("WEIGHTS_0") != primitive.attributes.end());
	REQUIRE(after.images.size() == 1);
	const auto image = [](const fastgltf::Asset &asset)
	{
		const auto &view = std::get<fastgltf::sources::BufferView>(asset.images[0].data);
		const auto bytes = fastgltf::DefaultBufferDataAdapter{}(asset, view.bufferViewIndex);
		return std::vector<std::byte>{bytes.begin(), bytes.end()};
	};
	CHECK(image(after) == image(before));
	CHECK(
	    std::get<fastgltf::sources::BufferView>(after.images[0].data).mimeType ==
	    fastgltf::MimeType::PNG);
}


TEST_CASE("A cooked mesh of several primitives has each, with its material, in every level")
{
	const auto file = Cook(ReadContent("models/crate.glb"), "crate.glb");
	const auto &mesh = file->Asset().meshes[0];
	for (const Geometry::MeshLevel &level : file->Levels(0))
	{
		REQUIRE(level.primitiveCount == 2);
		CHECK(mesh.primitives[level.firstPrimitive].materialIndex == 0u);
		CHECK(mesh.primitives[level.firstPrimitive + 1].materialIndex == 1u);
	}
}


TEST_CASE("A file that was not cooked is one level of error 0, with no cook key")
{
	const auto file = Geometry::GltfFile::Parse(ReadContent("models/crate.glb"), "crate.glb");
	REQUIRE(file->Levels(0).size() == 1);
	CHECK(file->Levels(0).front().primitiveCount == 2);
	CHECK(file->Levels(0).front().error == 0.0f);
	CHECK(file->CookKey().empty());
}


TEST_CASE("The cook refuses what it cannot cook, naming the file")
{
	const auto bytes = MakeRock(false);
	const auto source = Geometry::GltfFile::Parse(bytes, "sceneless.glb");
	CHECK_THROWS_WITH_AS(
	    Geometry::CookModel(*source, {}, "key"), doctest::Contains("sceneless.glb"),
	    std::runtime_error);

	const auto rock = MakeRock();
	const auto withScene = Geometry::GltfFile::Parse(rock, "rock.glb");
	Geometry::LodSettings settings;
	settings.reduction = 1.0f;
	CHECK_THROWS_WITH_AS(
	    Geometry::CookModel(*withScene, settings, "key"), doctest::Contains("rock.glb"),
	    std::runtime_error);
}
