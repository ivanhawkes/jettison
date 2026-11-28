#include <doctest/doctest.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <jettison/core/Log.h>
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


// One triangle with a COLOR_0 and a WEIGHTS_0 of these values, a vertex each.
std::vector<std::byte> MakeTriangle(
    const std::vector<glm::vec4> &colours, const std::vector<glm::vec4> &weights)
{
	const std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	fastgltf::Asset asset;
	std::vector<std::byte> bytes;
	Append(bytes, positions);
	Append(bytes, colours);
	Append(bytes, weights);
	fastgltf::Buffer buffer{};
	buffer.byteLength = bytes.size();
	buffer.data = fastgltf::sources::Vector{std::move(bytes)};
	asset.buffers.push_back(std::move(buffer));
	const std::size_t sizes[] = {sizeof(glm::vec3) * 3, sizeof(glm::vec4) * 3, sizeof(glm::vec4) * 3};
	std::size_t offset = 0;
	for (std::size_t index = 0; index < 3; ++index)
	{
		fastgltf::BufferView view{};
		view.bufferIndex = 0;
		view.byteOffset = offset;
		view.byteLength = sizes[index];
		asset.bufferViews.push_back(std::move(view));
		offset += sizes[index];
		fastgltf::Accessor accessor{};
		accessor.bufferViewIndex = index;
		accessor.count = 3;
		accessor.type = index == 0 ? fastgltf::AccessorType::Vec3 : fastgltf::AccessorType::Vec4;
		accessor.componentType = fastgltf::ComponentType::Float;
		asset.accessors.push_back(std::move(accessor));
	}
	fastgltf::Primitive primitive{};
	primitive.attributes = {{"POSITION", 0}, {"COLOR_0", 1}, {"WEIGHTS_0", 2}};
	fastgltf::Mesh mesh{};
	mesh.name = "triangle";
	mesh.primitives.push_back(std::move(primitive));
	asset.meshes.push_back(std::move(mesh));
	fastgltf::Node node{};
	node.meshIndex = 0;
	asset.nodes.push_back(std::move(node));
	asset.scenes.push_back({{0}, "scene"});
	asset.defaultScene = 0;

	fastgltf::Exporter exporter;
	auto written = exporter.writeGltfBinary(asset);
	REQUIRE(written.error() == fastgltf::Error::None);
	return std::move(written.get().output);
}


// An attribute of the cooked triangle's first primitive, by the vertex's position, since the cook
// reorders vertices.
std::map<std::pair<float, float>, glm::vec4> ByPosition(
    const fastgltf::Asset &asset, const char *attribute)
{
	const auto &primitive = asset.meshes[0].primitives[0];
	const auto positions = Positions(asset, primitive);
	std::map<std::pair<float, float>, glm::vec4> values;
	fastgltf::iterateAccessorWithIndex<glm::vec4>(
	    asset, asset.accessors[primitive.findAttribute(attribute)->accessorIndex],
	    [&](glm::vec4 value, std::size_t index)
	    { values[{positions[index].x, positions[index].y}] = value; });
	return values;
}


// A .gltf of one triangle whose positions are in a file of their own at uri.
std::string TriangleWithBuffer(const std::string &uri)
{
	return R"({"asset":{"version":"2.0"},"buffers":[{"uri":")" + uri +
	       R"(","byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],)"
	       R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)"
	       R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],)"
	       R"("scenes":[{"nodes":[0]}],"scene":0})";
}


std::span<const std::byte> AsBytes(const std::string &text)
{
	return std::as_bytes(std::span{text});
}


// A .gltf whose buffer 0 is three positions, (0, 0, 0), (1, 0, 0) and (0, 1, 0), embedded, with the
// rest of its buffers and the rest of the file after it.
std::string WithTriangleData(const std::string &rest)
{
	return R"({"asset":{"version":"2.0"},"buffers":[{"uri":"data:application/octet-stream;base64,)"
	       R"(AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA","byteLength":36})" +
	       rest + "}";
}


// One mesh node, its mesh one primitive of these attributes, and these mesh extras.
std::string OneMesh(const std::string &attributes, const std::string &extras = "{}")
{
	return R"("meshes":[{"primitives":[{"attributes":)" + attributes + R"(}],"extras":)" + extras +
	       R"(}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)";
}


// The triangle, with these accessors on a view of all of buffer 0, and the rest of the file after.
std::string TriangleAccessors(const std::string &accessors, const std::string &rest)
{
	return WithTriangleData(
	    R"(],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":)" + accessors + "," + rest);
}


// The triangle's positions as one view compressed with these parameters, decoding into buffer 1.
std::string Compressed(
    const char *mode, std::size_t stride, std::size_t count, const char *offset = "0",
    const char *source = "0")
{
	return WithTriangleData(
	    R"(,{"byteLength":36}],"extensionsUsed":["EXT_meshopt_compression"],)"
	    R"("extensionsRequired":["EXT_meshopt_compression"],"bufferViews":[{"buffer":1,"byteLength":)" +
	    std::to_string(stride * count) + R"(,"extensions":{"EXT_meshopt_compression":{"buffer":)" +
	    source + R"(,"byteOffset":)" + offset + R"(,"byteLength":36,"byteStride":)" +
	    std::to_string(stride) + R"(,"count":)" + std::to_string(count) + R"(,"mode":")" + mode +
	    R"("}}}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)" +
	    OneMesh(R"({"POSITION":0})"));
}


// A mesh of these triangles and no normals, its one set of vertices named by this many primitives,
// and the mesh by this many nodes.
std::vector<std::byte> MakeMesh(
    const std::vector<glm::vec3> &positions, const std::vector<std::uint32_t> &indices,
    std::size_t primitives = 1, std::size_t nodes = 1)
{
	fastgltf::Asset asset;
	std::vector<std::byte> bytes;
	Append(bytes, positions);
	Append(bytes, indices);
	fastgltf::Buffer buffer{};
	buffer.byteLength = bytes.size();
	buffer.data = fastgltf::sources::Vector{std::move(bytes)};
	asset.buffers.push_back(std::move(buffer));
	const std::size_t positionBytes = positions.size() * sizeof(glm::vec3);
	asset.bufferViews.push_back({});
	asset.bufferViews[0].byteLength = positionBytes;
	asset.bufferViews.push_back({});
	asset.bufferViews[1].byteOffset = positionBytes;
	asset.bufferViews[1].byteLength = indices.size() * sizeof(std::uint32_t);
	for (const auto &[view, count, type, component] :
	     {std::tuple{
	          std::size_t{0}, positions.size(), fastgltf::AccessorType::Vec3,
	          fastgltf::ComponentType::Float},
	      std::tuple{
	          std::size_t{1}, indices.size(), fastgltf::AccessorType::Scalar,
	          fastgltf::ComponentType::UnsignedInt}})
	{
		fastgltf::Accessor accessor{};
		accessor.bufferViewIndex = view;
		accessor.count = count;
		accessor.type = type;
		accessor.componentType = component;
		asset.accessors.push_back(std::move(accessor));
	}
	fastgltf::Mesh mesh{};
	for (std::size_t index = 0; index < primitives; ++index)
	{
		fastgltf::Primitive primitive{};
		primitive.attributes = {{"POSITION", 0}};
		primitive.indicesAccessor = 1;
		mesh.primitives.push_back(std::move(primitive));
	}
	asset.meshes.push_back(std::move(mesh));
	for (std::size_t index = 0; index < nodes; ++index)
	{
		fastgltf::Node node{};
		node.meshIndex = 0;
		asset.nodes.push_back(std::move(node));
	}
	asset.scenes.push_back({{0}, "scene"});
	asset.defaultScene = 0;

	fastgltf::Exporter exporter;
	auto written = exporter.writeGltfBinary(asset);
	REQUIRE(written.error() == fastgltf::Error::None);
	return std::move(written.get().output);
}


// A strip of triangles over this many vertices, each vertex used, with no normals.
std::vector<std::byte> MakeStrip(std::uint32_t vertexCount)
{
	std::vector<glm::vec3> positions;
	positions.reserve(vertexCount);
	for (std::uint32_t vertex = 0; vertex < vertexCount; ++vertex)
	{
		const std::uint32_t column = vertex / 2;
		positions.emplace_back(static_cast<float>(column), static_cast<float>(vertex % 2), 0.0f);
	}
	std::vector<std::uint32_t> indices;
	for (std::uint32_t first = 0; first + 2 < vertexCount; ++first)
		indices.insert(indices.end(), {first, first + 1, first + 2});
	return MakeMesh(positions, indices);
}


// A square grid of side by side vertices, two triangles a cell, gently bumped so the simplifier has
// something to keep and much to take away (share.py's), with no normals.
std::vector<std::byte> MakeGrid(std::uint32_t side, std::size_t primitives = 1, std::size_t nodes = 1)
{
	std::vector<glm::vec3> positions;
	for (std::uint32_t row = 0; row < side; ++row)
		for (std::uint32_t column = 0; column < side; ++column)
		{
			const auto x = static_cast<float>(column);
			const auto z = static_cast<float>(row);
			positions.emplace_back(x, 0.1f * std::sin(0.3f * x) * std::cos(0.3f * z), z);
		}
	std::vector<std::uint32_t> indices;
	for (std::uint32_t row = 0; row + 1 < side; ++row)
		for (std::uint32_t column = 0; column + 1 < side; ++column)
		{
			const std::uint32_t corner = (row * side) + column;
			indices.insert(
			    indices.end(),
			    {corner, corner + side, corner + 1, corner + 1, corner + side, corner + side + 1});
		}
	return MakeMesh(positions, indices, primitives, nodes);
}


// A GLB with one edit to its JSON chunk's text, from the first from to to, padded as the format
// requires.
std::vector<std::byte> EditJson(
    const std::vector<std::byte> &glb, const std::string &from, const std::string &to)
{
	std::uint32_t length{};
	std::memcpy(&length, glb.data() + 12, sizeof length);
	std::string json{reinterpret_cast<const char *>(glb.data() + 20), length};
	const auto found = json.find(from);
	REQUIRE(found != std::string::npos);
	json.replace(found, from.size(), to);
	json.resize((json.size() + 3) / 4 * 4, ' ');
	const auto jsonLength = static_cast<std::uint32_t>(json.size());
	const auto total = static_cast<std::uint32_t>(glb.size() - length + json.size());
	std::vector<std::byte> edited{glb.begin(), glb.begin() + 12};
	std::memcpy(edited.data() + 8, &total, sizeof total);
	const auto *pLength = reinterpret_cast<const std::byte *>(&jsonLength);
	edited.insert(edited.end(), pLength, pLength + sizeof jsonLength);
	edited.insert(edited.end(), glb.begin() + 16, glb.begin() + 20);
	const auto *pJson = reinterpret_cast<const std::byte *>(json.data());
	edited.insert(edited.end(), pJson, pJson + json.size());
	edited.insert(edited.end(), glb.begin() + 20 + length, glb.end());
	return edited;
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


TEST_CASE(
    "A colour outside [0, 1] is kept exactly as float, and a weight outside it is clamped, with a "
    "warning")
{
	std::vector<std::string> warnings;
	const Core::LogCallbackId callback = Core::AddLogCallback(
	    [&](const Core::LogRecord &record)
	    {
		    if (record.severity == Core::Severity::Warning)
			    warnings.emplace_back(record.message);
	    });
	const auto file = Cook(
	    MakeTriangle(
	        {{1.5f, -0.25f, 0.5f, 1.0f}, {0.0f, 1.0f, 2.0f, 1.0f}, {0.25f, 0.5f, 0.75f, 1.0f}},
	        {{1.25f, 0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}),
	    "triangle.glb");
	Core::RemoveLogCallback(callback);
	const auto &asset = file->Asset();
	const auto &primitive = asset.meshes[0].primitives[0];

	const auto &colourAccessor = asset.accessors[primitive.findAttribute("COLOR_0")->accessorIndex];
	CHECK(colourAccessor.componentType == fastgltf::ComponentType::Float);
	auto colours = ByPosition(asset, "COLOR_0");
	CHECK(colours[{0.0f, 0.0f}] == glm::vec4{1.5f, -0.25f, 0.5f, 1.0f});
	CHECK(colours[{1.0f, 0.0f}] == glm::vec4{0.0f, 1.0f, 2.0f, 1.0f});
	CHECK(colours[{0.0f, 1.0f}] == glm::vec4{0.25f, 0.5f, 0.75f, 1.0f});

	const auto &weightAccessor = asset.accessors[primitive.findAttribute("WEIGHTS_0")->accessorIndex];
	CHECK(weightAccessor.componentType == fastgltf::ComponentType::UnsignedShort);
	CHECK(weightAccessor.normalized);
	auto weights = ByPosition(asset, "WEIGHTS_0");
	CHECK(weights[{0.0f, 0.0f}].x == 1.0f);
	CHECK(weights[{1.0f, 0.0f}].x == doctest::Approx(0.5f).epsilon(0.0001));
	REQUIRE(warnings.size() == 1);
	CHECK(warnings[0].find("triangle.glb") != std::string::npos);
	CHECK(warnings[0].find("WEIGHTS_0") != std::string::npos);
}


TEST_CASE("Colours and weights within [0, 1] are stored in 16 bits normalised")
{
	const auto file = Cook(
	    MakeTriangle(
	        {{1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.5f, 1.0f}, {0.25f, 0.5f, 0.75f, 1.0f}},
	        {{1.0f, 0.0f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 0.0f}}),
	    "triangle.glb");
	const auto &asset = file->Asset();
	const auto &primitive = asset.meshes[0].primitives[0];
	for (const char *attribute : {"COLOR_0", "WEIGHTS_0"})
	{
		const auto &accessor = asset.accessors[primitive.findAttribute(attribute)->accessorIndex];
		CHECK(accessor.componentType == fastgltf::ComponentType::UnsignedShort);
		CHECK(accessor.normalized);
	}
	CHECK(ByPosition(asset, "COLOR_0")[{0.0f, 0.0f}] == glm::vec4{1.0f, 0.0f, 1.0f, 1.0f});
}


TEST_CASE("A cooked model given KTX 2 images names them through KHR_texture_basisu")
{
	const auto bytes = ReadContent("models/arm.glb");
	const auto source = Geometry::GltfFile::Parse(bytes, "arm.glb");
	const std::vector<std::vector<std::byte>> images{{std::byte{1}, std::byte{2}, std::byte{3}}};
	const auto cooked = Geometry::CookModel(*source, {}, "key", images);
	const auto file = Geometry::GltfFile::Parse(cooked, "arm.glb");
	const auto &asset = file->Asset();

	for (const auto *pList : {&asset.extensionsUsed, &asset.extensionsRequired})
		CHECK(std::ranges::find(*pList, "KHR_texture_basisu") != pList->end());
	REQUIRE(asset.textures.size() == source->Asset().textures.size());
	for (const auto &texture : asset.textures)
	{
		CHECK_FALSE(texture.imageIndex.has_value());
		CHECK(texture.basisuImageIndex.has_value());
	}
	const auto &view = std::get<fastgltf::sources::BufferView>(asset.images[0].data);
	CHECK(view.mimeType == fastgltf::MimeType::KTX2);
	const auto stored = fastgltf::DefaultBufferDataAdapter{}(asset, view.bufferViewIndex);
	CHECK(std::vector<std::byte>{stored.begin(), stored.end()} == images[0]);

	const std::vector<std::vector<std::byte>> tooMany(2);
	CHECK_THROWS_WITH_AS(
	    Geometry::CookModel(*source, {}, "key", tooMany), doctest::Contains("arm.glb"),
	    std::runtime_error);
}


TEST_CASE("A .gltf's buffer in a file of its own is read from its directory, and only from within it")
{
	const auto directory = std::filesystem::temp_directory_path() / "jettison-geometry-buffers";
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory / "inside");
	const std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	for (const auto &path : {directory / "inside" / "triangle.bin", directory / "outside.bin"})
	{
		std::ofstream stream{path, std::ios::binary};
		stream.write(reinterpret_cast<const char *>(positions.data()), sizeof(glm::vec3) * 3);
	}
	const std::string gltf = TriangleWithBuffer("triangle.bin");

	const auto file = Geometry::GltfFile::Parse(AsBytes(gltf), "triangle.gltf", directory / "inside");
	CHECK(Positions(file->Asset(), file->Asset().meshes[0].primitives[0]) == positions);

	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(AsBytes(gltf), "triangle.gltf"), doctest::Contains("triangle.gltf"),
	    std::runtime_error);
	std::filesystem::create_symlink(directory / "outside.bin", directory / "inside" / "link.bin");
	for (const std::string &uri :
	     {std::string{"../outside.bin"}, (directory / "outside.bin").string(), std::string{"link.bin"}})
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(
		        AsBytes(TriangleWithBuffer(uri)), "escape.gltf", directory / "inside"),
		    doctest::Contains("outside its directory: escape.gltf"), std::runtime_error);
	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(
	        AsBytes(TriangleWithBuffer("missing.bin")), "missing.gltf", directory / "inside"),
	    doctest::Contains("missing.gltf"), std::runtime_error);
	std::filesystem::remove_all(directory);
}


TEST_CASE(
    "A .gltf's buffer file is read only as far as its buffers declare, and they are held to 1 GiB")
{
	const auto directory = std::filesystem::temp_directory_path() / "jettison-geometry-buffer-lengths";
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory);
	std::vector<glm::vec3> positions{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
	positions.resize(1000, glm::vec3{7.0f});
	{
		std::ofstream stream{directory / "long.bin", std::ios::binary};
		stream.write(
		    reinterpret_cast<const char *>(positions.data()),
		    static_cast<std::streamsize>(positions.size() * sizeof(glm::vec3)));
	}
	const auto withBuffers = [](const std::string &buffers)
	{
		return R"({"asset":{"version":"2.0"},"buffers":[)" + buffers +
		       R"(],"bufferViews":[{"buffer":0,"byteLength":36}],)"
		       R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],)" +
		       OneMesh(R"({"POSITION":0})") + "}";
	};

	// Two buffers naming one file of 12,000 bytes (uri.gltf's 16 named one of 64 MiB, each read whole).
	const auto file = Geometry::GltfFile::Parse(
	    AsBytes(
	        withBuffers(R"({"uri":"long.bin","byteLength":36},{"uri":"./long.bin","byteLength":24})")),
	    "long.gltf", directory);
	const auto &buffers = file->Asset().buffers;
	REQUIRE(buffers.size() == 2);
	for (std::size_t index = 0; index < 2; ++index)
	{
		const auto &bytes = std::get<fastgltf::sources::Vector>(buffers[index].data).bytes;
		REQUIRE(bytes.size() == buffers[index].byteLength);
		CHECK(std::memcmp(bytes.data(), positions.data(), bytes.size()) == 0);
	}
	CHECK(
	    Positions(file->Asset(), file->Asset().meshes[0].primitives[0]) ==
	    std::vector(positions.begin(), positions.begin() + 3));

	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(
	        AsBytes(withBuffers(
	            R"({"uri":"long.bin","byteLength":600000000},)"
	            R"({"uri":"long.bin","byteLength":600000000})")),
	        "vast.gltf", directory),
	    doctest::Contains("buffer files are declared longer than 1073741824 bytes together: vast.gltf"),
	    std::runtime_error);
	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(
	        AsBytes(withBuffers(R"({"uri":"long.bin","byteLength":12004})")), "short.gltf", directory),
	    doctest::Contains("shorter than the buffer: short.gltf"), std::runtime_error);
	std::filesystem::remove_all(directory);
}


TEST_CASE(
    "An accessor outside its view or buffer, or a primitive's attributes of two counts, is refused")
{
	constexpr const char *kPositions =
	    R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"})";
	const std::string twoPositions = R"({"bufferView":0,"componentType":5126,"count":2,"type":"VEC3"})";
	const std::string position = OneMesh(R"({"POSITION":0})");
	const std::pair<std::string, const char *> cases[] = {
	    // Normals past the positions' count, which a reader sized by POSITION wrote past.
	    {TriangleAccessors(
	         "[" + twoPositions + "," + kPositions + "]", OneMesh(R"({"POSITION":0,"NORMAL":1})")),
	     "different counts"},
	    {TriangleAccessors(
	         R"([{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"}])", position),
	     "outside its view or buffer"},
	    {TriangleAccessors(
	         R"([{"bufferView":0,"byteOffset":4,"componentType":5126,"count":3,"type":"VEC3"}])",
	         position),
	     "outside its view or buffer"},
	    {WithTriangleData(
	         R"(],"bufferViews":[{"buffer":0,"byteLength":48}],"accessors":[)" +
	         std::string{kPositions} + "]," + position),
	     "buffer view 0 lies outside its buffer"},
	    {TriangleAccessors(
	         R"([{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","sparse":{"count":1,)"
	         R"("indices":{"bufferView":0,"componentType":5125},"values":{"bufferView":0}}}])",
	         position),
	     "sparse"},
	    {TriangleAccessors(
	         "[" + std::string{kPositions} + "]", OneMesh(R"({"POSITION":0,"NORMAL":7})")),
	     "lacks"},
	    {TriangleAccessors(
	         "[" + std::string{kPositions} + "]",
	         OneMesh(R"({"POSITION":0})") + R"(,"skins":[{"joints":[0],"inverseBindMatrices":7}])"),
	     "lacks"},
	};
	for (const auto &[gltf, reason] : cases)
	{
		CAPTURE(gltf);
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "bad.gltf"), doctest::Contains(reason),
		    std::runtime_error);
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "bad.gltf"), doctest::Contains("bad.gltf"),
		    std::runtime_error);
	}
	CHECK(
	    Geometry::GltfFile::Parse(
	        AsBytes(TriangleAccessors("[" + std::string{kPositions} + "]", position)), "good.gltf"));
}


TEST_CASE(
    "A compressed view that breaks EXT_meshopt_compression, or lies outside its buffers, is refused")
{
	const std::pair<std::string, const char *> cases[] = {
	    {Compressed("ATTRIBUTES", 3, 12), "breaks EXT_meshopt_compression"},
	    {Compressed("TRIANGLES", 2, 5), "breaks EXT_meshopt_compression"},
	    {Compressed("TRIANGLES", 3, 3), "breaks EXT_meshopt_compression"},
	    {Compressed("INDICES", 1, 36), "breaks EXT_meshopt_compression"},
	    // An offset that wraps past the end of memory to land within the buffer.
	    {Compressed("ATTRIBUTES", 4, 9, "18446744073709551600"), "out of its buffers' range"},
	    // More than the codec decodes 36 bytes to: 1,024 bytes each for attributes, and 37 bytes
	    // needed for 20 triangles, or for 32 indices.
	    {Compressed("ATTRIBUTES", 4, 9217), "claims more than meshoptimizer's codec decodes"},
	    {Compressed("ATTRIBUTES", 4, 67108864), "claims more than meshoptimizer's codec decodes"},
	    {Compressed("TRIANGLES", 2, 60), "claims more than meshoptimizer's codec decodes"},
	    {Compressed("INDICES", 4, 32), "claims more than meshoptimizer's codec decodes"},
	};
	for (const auto &[gltf, reason] : cases)
	{
		CAPTURE(gltf);
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "compressed.gltf"), doctest::Contains(reason),
		    std::runtime_error);
	}
}


TEST_CASE(
    "Levels that are empty, whose finest error is not 0, or whose errors are negative, past a float or "
    "shrinking, are refused")
{
	constexpr const char *kPositions =
	    R"([{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}])";
	for (const char *levels : {"[]", R"([{"primitives":1,"error":1}])"})
	{
		CAPTURE(levels);
		const std::string gltf = TriangleAccessors(
		    kPositions,
		    OneMesh(R"({"POSITION":0})", std::string{R"({"jettison":{"levels":)"} + levels + "}}"));
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "levels.gltf"),
		    doctest::Contains("extras are malformed: levels.gltf"), std::runtime_error);
	}

	const auto twoLevels = [&](const char *finest, const char *second)
	{
		return TriangleAccessors(
		    kPositions,
		    std::string{
		        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}},{"attributes":{"POSITION":0}}],)"} +
		        R"("extras":{"jettison":{"levels":[{"primitives":1,"error":)" + finest +
		        R"(},{"primitives":1,"error":)" + second +
		        R"(}]}}}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)");
	};

	// The finest level's error, which the format makes 0, given as 1 to both (finest-error.gltf).
	for (const auto &[finest, second] : {std::pair{"1", "1"}, {"0", "-0.25"}, {"0", "1e300"}})
	{
		CAPTURE(finest);
		CAPTURE(second);
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(twoLevels(finest, second)), "levels.gltf"),
		    doctest::Contains("extras are malformed"), std::runtime_error);
	}
	const auto file = Geometry::GltfFile::Parse(AsBytes(twoLevels("0", "0.5")), "levels.gltf");
	REQUIRE(file->Levels(0).size() == 2);
	CHECK(file->Levels(0)[1].error == 0.5f);
}


TEST_CASE("A fallback buffer is made as long as its views reach, and one reaching too far is refused")
{
	const auto source = Geometry::GltfFile::Parse(MakeRock(), "rock.glb");
	const auto cooked = Geometry::CookModel(*source, {}, "key");
	const auto declared = [&](const std::vector<std::byte> &glb)
	{
		const auto file = Geometry::GltfFile::Parse(glb, "rock.glb");
		const auto &buffer = file->Asset().buffers[1];
		return std::get<fastgltf::sources::Vector>(buffer.data).bytes.size();
	};
	const std::size_t length = declared(cooked);
	const std::string fallback = R"({"fallback":true}},"byteLength":)";

	// A terabyte declared, which was allocated, and the views' reach made.
	const auto vast = EditJson(cooked, fallback + std::to_string(length), fallback + "1099511627776");
	CHECK(declared(vast) == length);

	// A view placed a terabyte in, within what the buffer declares: past the buffer's limit.
	const auto far = EditJson(
	    EditJson(cooked, fallback + std::to_string(length), fallback + "2199023255552"),
	    R"({"buffer":1,)", R"({"buffer":1,"byteOffset":1099511627776,)");
	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(far, "far.glb"),
	    doctest::Contains("fallback buffers decode to more than 1073741824 bytes: far.glb"),
	    std::runtime_error);

	// Four fallbacks of four views at the view's limit, each naming the same stream (bomb.gltf, 2 KB,
	// once 16 bytes, now the 256 KiB the codec needs for the view): refused on the views alone, before
	// 4 GiB is allocated for them.
	std::string buffers =
	    R"("buffers":[{"byteLength":262146,"uri":"data:application/octet-stream;base64,)" +
	    std::string(349528, 'A') + R"("})";
	std::string views = R"("bufferViews":[)";
	for (int buffer = 1; buffer <= 4; ++buffer)
	{
		buffers +=
		    R"(,{"byteLength":1073741824,"extensions":{"EXT_meshopt_compression":{"fallback":true}}})";
		for (int view = 0; view < 4; ++view)
			views +=
			    std::string{buffer == 1 && view == 0 ? "" : ","} + R"({"buffer":)" +
			    std::to_string(buffer) + R"(,"byteOffset":)" + std::to_string(view * 268435456) +
			    R"(,"byteLength":268435456,"byteStride":4,"extensions":{"EXT_meshopt_compression":)"
			    R"({"buffer":0,"byteLength":262146,"byteStride":4,"count":67108864,)"
			    R"("mode":"ATTRIBUTES"}}})";
	}
	const std::string bomb =
	    R"({"asset":{"version":"2.0"},"extensionsUsed":["EXT_meshopt_compression"],)"
	    R"("extensionsRequired":["EXT_meshopt_compression"],"scenes":[{"nodes":[]}],)" +
	    buffers + "]," + views + "]}";
	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(AsBytes(bomb), "bomb.gltf"),
	    doctest::Contains("fallback buffers decode to more than 1073741824 bytes: bomb.gltf"),
	    std::runtime_error);

	// A view that would decode to more than its limit.
	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(AsBytes(Compressed("ATTRIBUTES", 256, 1048577)), "large.gltf"),
	    doctest::Contains("decodes to more than 268435456 bytes: large.gltf"), std::runtime_error);
}


TEST_CASE("An accessor of a type or component type its use does not allow is refused")
{
	constexpr const char *kPositions =
	    R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"})";
	const auto withSecond = [&](const std::string &second, const std::string &rest)
	{ return TriangleAccessors("[" + std::string{kPositions} + "," + second + "]", rest); };
	const std::string times = R"({"bufferView":0,"componentType":5126,"count":3,"type":"SCALAR"})";
	const auto clip = [](const char *path)
	{
		return OneMesh(R"({"POSITION":0})") +
		       R"(,"animations":[{"samplers":[{"input":1,"output":0}],)"
		       R"("channels":[{"sampler":0,"target":{"node":0,"path":")" +
		       std::string{path} + R"("}}]}])";
	};
	const std::pair<std::string, const char *> cases[] = {
	    // A heap overflow, and fastgltf's assertion, when position-vec2 and position-scalar-read were
	    // read as positions.
	    {TriangleAccessors(
	         R"([{"bufferView":0,"componentType":5126,"count":3,"type":"VEC2"}])",
	         OneMesh(R"({"POSITION":0})")),
	     "accessor 0 is not a type or component type glTF allows for POSITION"},
	    {TriangleAccessors(
	         R"([{"bufferView":0,"componentType":5126,"count":3,"type":"SCALAR"}])",
	         OneMesh(R"({"POSITION":0})")),
	     "allows for POSITION"},
	    {withSecond(
	         R"({"bufferView":0,"componentType":5121,"normalized":true,"count":3,"type":"VEC3"})",
	         OneMesh(R"({"POSITION":0,"NORMAL":1})")),
	     "allows for NORMAL"},
	    {withSecond(
	         R"({"bufferView":0,"componentType":5121,"normalized":true,"count":3,"type":"VEC4"})",
	         OneMesh(R"({"POSITION":0,"JOINTS_0":1})")),
	     "allows for JOINTS_0"},
	    {withSecond(
	         R"({"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"})",
	         OneMesh(R"({"POSITION":0,"TEXCOORD_0":1})")),
	     "allows for TEXCOORD_0"},
	    {withSecond(
	         times, R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
	                R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)"),
	     "allows for indices"},
	    {withSecond(
	         R"({"bufferView":0,"componentType":5123,"normalized":true,"count":3,"type":"SCALAR"})",
	         R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
	         R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)"),
	     "allows for indices"},
	    {withSecond(
	         times,
	         OneMesh(R"({"POSITION":0})") + R"(,"skins":[{"joints":[0],"inverseBindMatrices":0}])"),
	     "allows for inverse-bind matrices"},
	    {withSecond(times, clip("rotation")), "allows for a clip's rotations"},
	    {withSecond(times, clip("weights")), "allows for a clip's weights"},
	    {TriangleAccessors(
	         "[" + std::string{kPositions} + "]",
	         OneMesh(R"({"POSITION":0})") +
	             R"(,"animations":[{"samplers":[{"input":0,"output":0}],)"
	             R"("channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}])"),
	     "allows for a clip's times"},
	};
	for (const auto &[gltf, reason] : cases)
	{
		CAPTURE(gltf);
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "typed.gltf"),
		    doctest::Contains(std::string{reason} + ": typed.gltf"), std::runtime_error);
	}

	// Quantised positions, as KHR_mesh_quantization allows, an application's own attribute of any
	// type, and a clip's translations.
	for (const std::string &gltf :
	     {TriangleAccessors(
	          R"([{"bufferView":0,"componentType":5122,"normalized":true,"count":3,"type":"VEC3"}])",
	          OneMesh(R"({"POSITION":0})")),
	      withSecond(
	          R"({"bufferView":0,"componentType":5125,"count":3,"type":"SCALAR"})",
	          OneMesh(R"({"POSITION":0,"_HEAT":1})")),
	      withSecond(times, clip("translation"))})
	{
		CAPTURE(gltf);
		CHECK(Geometry::GltfFile::Parse(AsBytes(gltf), "typed.gltf"));
	}
}


TEST_CASE("An index naming what the file lacks, or nodes that are not trees, is refused")
{
	const std::string positions = R"([{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}])";
	const std::string position = OneMesh(R"({"POSITION":0})");
	const auto withNodes = [&](const std::string &nodes)
	{
		return TriangleAccessors(
		    positions, R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":)" + nodes +
		                   R"(,"scenes":[{"nodes":[0]}],"scene":0)");
	};
	const std::pair<std::string, const char *> cases[] = {
	    // A read through a wild pointer when image-view's image was read.
	    {TriangleAccessors(
	         positions, position + R"(,"images":[{"bufferView":999,"mimeType":"image/png"}])"),
	     "image 0 names buffer view 999"},
	    {WithTriangleData(
	         R"(],"bufferViews":[{"buffer":5,"byteLength":36}],"accessors":)" + positions + "," +
	         position),
	     "buffer view 0 names buffer 5"},
	    // A compressed view's source out of range (badbuf.gltf), once a std::out_of_range naming no
	    // file.
	    {Compressed("ATTRIBUTES", 4, 9, "0", "7"), "buffer view 0 names buffer 7"},
	    {TriangleAccessors(
	         R"([{"bufferView":4,"componentType":5126,"count":3,"type":"VEC3"}])", position),
	     "accessor 0 names buffer view 4"},
	    {TriangleAccessors(positions, position + R"(,"textures":[{"source":3}])"),
	     "texture 0 names image 3"},
	    {TriangleAccessors(
	         positions,
	         position + R"(,"textures":[{"extensions":{"KHR_texture_basisu":{"source":3}}}])"),
	     "texture 0 names image 3"},
	    {TriangleAccessors(
	         positions,
	         position + R"(,"images":[{"uri":"a.png"}],"textures":[{"source":0,"sampler":2}])"),
	     "texture 0 names sampler 2"},
	    {TriangleAccessors(
	         positions,
	         position + R"(,"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":3}}}])"),
	     "material 0 names texture 3"},
	    {TriangleAccessors(positions, position + R"(,"materials":[{"emissiveTexture":{"index":3}}])"),
	     "material 0 names texture 3"},
	    {TriangleAccessors(
	         positions, R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":2}]}],)"
	                    R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)"),
	     "mesh 0 names material 2"},
	    {withNodes(R"([{"mesh":3}])"), "node 0 names mesh 3"},
	    {withNodes(R"([{"mesh":0,"skin":3}])"), "node 0 names skin 3"},
	    {withNodes(R"([{"mesh":0,"camera":3}])"), "node 0 names camera 3"},
	    {withNodes(R"([{"mesh":0,"children":[3]}])"), "node 0 names node 3"},
	    {TriangleAccessors(positions, position + R"(,"skins":[{"joints":[3]}])"),
	     "skin 0 names node 3"},
	    {TriangleAccessors(positions, position + R"(,"skins":[{"joints":[0],"skeleton":3}])"),
	     "skin 0 names node 3"},
	    {TriangleAccessors(
	         positions, R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
	                    R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[3]}],"scene":0)"),
	     "scene 0 names node 3"},
	    {TriangleAccessors(
	         positions, R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
	                    R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":3)"),
	     "default scene is scene 3"},
	    {TriangleAccessors(
	         positions, position +
	                        R"(,"animations":[{"samplers":[{"input":0,"output":0}],)"
	                        R"("channels":[{"sampler":3,"target":{"node":0,"path":"scale"}}]}])"),
	     "animation 0 names sampler 3"},
	    {TriangleAccessors(
	         positions, position +
	                        R"(,"animations":[{"samplers":[{"input":0,"output":0}],)"
	                        R"("channels":[{"sampler":0,"target":{"node":3,"path":"scale"}}]}])"),
	     "animation 0 names node 3"},
	    {TriangleAccessors(
	         positions, position +
	                        R"(,"animations":[{"samplers":[{"input":0,"output":3}],)"
	                        R"("channels":[{"sampler":0,"target":{"node":0,"path":"scale"}}]}])"),
	     "animation 0 names accessor 3"},
	    {withNodes(R"([{"mesh":0,"children":[0]}])"), "a node that is its own ancestor"},
	    {withNodes(R"([{"mesh":0},{"children":[2]},{"children":[1]}])"),
	     "a node that is its own ancestor"},
	    {withNodes(R"([{"mesh":0,"children":[2]},{"children":[2]},{}])"), "node 2 has two parents"},
	};
	for (const auto &[gltf, reason] : cases)
	{
		CAPTURE(gltf);
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "indexed.gltf"), doctest::Contains(reason),
		    std::runtime_error);
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "indexed.gltf"), doctest::Contains("indexed.gltf"),
		    std::runtime_error);
	}
	CHECK(
	    Geometry::GltfFile::Parse(
	        AsBytes(withNodes(R"([{"children":[1,2]},{"mesh":0},{"children":[3]},{}])")),
	        "trees.gltf"));
}


TEST_CASE("An accessor with no view, all zeros, is refused, however few it reads as")
{
	// zeros.gltf's 22,369,620 positions, which the bake made into 2.8 GiB, and zero-accessor-huge's
	// count.
	for (const char *count : {"3", "22369620", "18446744073709551615"})
	{
		CAPTURE(count);
		const std::string gltf = TriangleAccessors(
		    std::string{R"([{"componentType":5126,"type":"VEC3","count":)"} + count + "}]",
		    OneMesh(R"({"POSITION":0})"));
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "zeros.gltf"),
		    doctest::Contains("accessor 0 has no buffer view: zeros.gltf"), std::runtime_error);
	}
}


TEST_CASE("What a file's primitives give its readers is held to a view's worth, each read counted")
{
	// The counts alone are checked, before any reader's bytes, so none needs to hold what it claims.
	const auto file = [](const std::string &count, const std::string &meshes, const char *nodes,
	                     const std::string &indices = "3")
	{
		return TriangleAccessors(
		    R"([{"bufferView":0,"componentType":5126,"type":"VEC3","count":)" + count +
		        R"(},{"bufferView":0,"componentType":5125,"type":"SCALAR","count":)" + indices + "}]",
		    R"("meshes":[{"primitives":)" + meshes + R"(}],"nodes":)" + nodes +
		        R"(,"scenes":[{"nodes":[0]}],"scene":0)");
	};
	const std::string one = R"([{"attributes":{"POSITION":0}}])";
	const std::string two = R"([{"attributes":{"POSITION":0}},{"attributes":{"POSITION":0}}])";
	const std::string apart = R"([{"attributes":{"POSITION":0}},{"attributes":{"POSITION":1}}])";
	const std::string half = "11184811";

	// A clip of one sampler of a million keys, named by this many channels, each of which the rig
	// reader reads apart (anim.py's, 128 channels, made it take 2.5 GiB).
	const auto clip = [](int channels)
	{
		std::string named;
		for (int channel = 0; channel < channels; ++channel)
			named += std::string{channel == 0 ? "" : ","} +
			         R"({"sampler":0,"target":{"node":0,"path":"translation"}})";
		return TriangleAccessors(
		    R"([{"bufferView":0,"componentType":5126,"type":"VEC3","count":3},)"
		    R"({"bufferView":0,"componentType":5126,"type":"SCALAR","count":1000000},)"
		    R"({"bufferView":0,"componentType":5126,"type":"VEC3","count":1000000}])",
		    OneMesh(R"({"POSITION":0})") +
		        R"(,"animations":[{"samplers":[{"input":1,"output":2}],"channels":[)" + named + "]}]");
	};
	const std::pair<std::string, const char *> refused[] = {
	    // Two primitives naming two accessors, which every reader reads apart.
	    {file(half, apart, R"([{"mesh":0}])", half),
	     "primitives give its readers more than 22369621 vertices"},
	    // One mesh in two nodes, which the bake reads twice.
	    {file(half, one, R"([{"mesh":0,"children":[1]},{"mesh":0}])"),
	     "primitives give its readers more than 22369621 vertices"},
	    {file("22369622", one, R"([{"mesh":0}])"),
	     "primitives give its readers more than 22369621 vertices"},
	    {file("3", R"([{"attributes":{"POSITION":0},"indices":1}])", R"([{"mesh":0}])", "67108865"),
	     "primitives give its readers more than 67108864 indices"},
	    // A primitive with no indices is drawn by as many as its vertices.
	    {TriangleAccessors(
	         R"([{"bufferView":0,"componentType":5126,"type":"VEC3","count":22369618},)"
	         R"({"bufferView":0,"componentType":5125,"type":"SCALAR","count":44739247},)"
	         R"({"bufferView":0,"componentType":5126,"type":"VEC3","count":3}])",
	         R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}},)"
	         R"({"attributes":{"POSITION":2},"indices":1}]}],"nodes":[{"mesh":0}],)"
	         R"("scenes":[{"nodes":[0]}],"scene":0)"),
	     "primitives give its readers more than 67108864 indices"},
	    {clip(9), "clips give its readers more than 16777216 keys"},
	};
	for (const auto &[gltf, reason] : refused)
	{
		CAPTURE(gltf.substr(0, 1000));
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "reads.gltf"),
		    doctest::Contains(std::string{reason} + " together: reads.gltf"), std::runtime_error);
	}

	// Within the limits, it is the bounds that refuse these counts: two primitives naming one
	// accessor, as a cooked file's levels do (z2k2.gltf), are read once by the renderer and the bake.
	for (const std::string &gltf :
	     {file(half, one, R"([{"mesh":0}])"), file(half, two, R"([{"mesh":0}])"), clip(8)})
	{
		CAPTURE(gltf.substr(0, 1000));
		CHECK_THROWS_WITH_AS(
		    Geometry::GltfFile::Parse(AsBytes(gltf), "reads.gltf"),
		    doctest::Contains("outside its view or buffer"), std::runtime_error);
	}
}


TEST_CASE(
    "Indices are 16-bit up to 65,535 vertices, so none is 65535, which glTF forbids, and 32-bit past")
{
	Geometry::LodSettings settings;
	settings.maxLevels = 1;
	for (const auto &[vertices, type] :
	     {std::pair{65535u, fastgltf::ComponentType::UnsignedShort},
	      std::pair{65536u, fastgltf::ComponentType::UnsignedInt}})
	{
		CAPTURE(vertices);
		const auto file = Cook(MakeStrip(vertices), "strip.glb", settings);
		const auto &asset = file->Asset();
		const auto &primitive = asset.meshes[0].primitives[0];
		REQUIRE(asset.accessors[primitive.findAttribute("POSITION")->accessorIndex].count == vertices);
		const auto &indices = asset.accessors[*primitive.indicesAccessor];
		CHECK(indices.componentType == type);
		std::uint32_t largest = 0;
		fastgltf::iterateAccessor<std::uint32_t>(
		    asset, indices, [&](std::uint32_t index) { largest = std::max(largest, index); });
		CHECK(largest == vertices - 1);
	}
}


TEST_CASE("A cooked file is one the reader takes, its levels' vertices counted once, or it is refused")
{
	// Twelve levels at most, as levels.yaml gave the grid of 2,250,000 vertices that cooked to a file
	// the reader refused, its eleven levels each counting the grid's vertices (grid.py).
	Geometry::LodSettings settings;
	settings.maxLevels = 12;
	const auto cooked = Cook(MakeGrid(40), "grid.glb", settings);
	const auto &asset = cooked->Asset();
	const std::size_t levels = cooked->Levels(0).size();
	REQUIRE(levels >= 5);
	const auto &primitives = asset.meshes[0].primitives;
	const std::size_t vertices =
	    asset.accessors[primitives[0].findAttribute("POSITION")->accessorIndex].count;
	std::size_t indices = 0;
	for (const auto &primitive : primitives)
		indices += asset.accessors[*primitive.indicesAccessor].count;

	// As many nodes naming the mesh, each a read, as its levels' indices allow: its vertices are
	// within the limit counted once, and past it counted for every level.
	const std::size_t nodes = 67108864 / indices;
	REQUIRE(nodes * vertices <= 22369621);
	REQUIRE(nodes * vertices * levels > 22369621);
	CHECK(Cook(MakeGrid(40, 1, nodes), "grid.glb", settings)->Levels(0).size() == levels);

	// As many as the source's own indices allow, which its levels' pass: refused, naming the source.
	const auto bytes = MakeGrid(40, 1, 67108864 / (39 * 39 * 6));
	const auto source = Geometry::GltfFile::Parse(bytes, "past.glb");
	CHECK_THROWS_WITH_AS(
	    Geometry::CookModel(*source, settings, "key"), doctest::Contains("Cannot cook past.glb: "),
	    std::runtime_error);
	CHECK_THROWS_WITH_AS(
	    Geometry::CookModel(*source, settings, "key"), doctest::Contains("more than 67108864 indices"),
	    std::runtime_error);
}


TEST_CASE("Primitives that share their vertices are read once, but the cook reads them apart")
{
	// A thousand primitives with no indices naming one accessor of 22,401 vertices, all zero: within
	// the limit read once, and past it read apart, as the cook reads each.
	std::string primitives;
	for (int primitive = 0; primitive < 1000; ++primitive)
		primitives += std::string{primitive == 0 ? "" : ","} + R"({"attributes":{"POSITION":0}})";
	const std::string gltf =
	    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":268812,)"
	    R"("uri":"data:application/octet-stream;base64,)" +
	    std::string(358416, 'A') +
	    R"("}],"bufferViews":[{"buffer":0,"byteLength":268812}],)"
	    R"("accessors":[{"bufferView":0,"componentType":5126,"count":22401,"type":"VEC3"}],)"
	    R"("meshes":[{"primitives":[)" +
	    primitives + R"(]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
	const auto source = Geometry::GltfFile::Parse(AsBytes(gltf), "soup.gltf");
	CHECK_THROWS_WITH_AS(
	    Geometry::CookModel(*source, {}, "key"),
	    doctest::Contains("Cannot cook soup.gltf: its primitives' vertices are more than 22369621"),
	    std::runtime_error);
}


TEST_CASE("Primitives that share their vertices are given normals summed once over them all")
{
	// share.py's mesh, many primitives naming one grid's vertices and indices, with no normals: each
	// primitive summed all of them: 1,600 of share.py's took 22.9 s in a Debug build, and these 1,000
	// 2.3 s, 0.12 s summed once (measured 2026-11-28).
	Geometry::LodSettings settings;
	settings.maxLevels = 1;
	constexpr std::size_t kPrimitives = 1000;
	const auto one = Cook(MakeGrid(17), "one.glb", settings);
	const auto many = Cook(MakeGrid(17, kPrimitives), "many.glb", settings);
	const auto normals = [](const fastgltf::Asset &asset, const fastgltf::Primitive &primitive)
	{
		const auto &accessor = asset.accessors[primitive.findAttribute("NORMAL")->accessorIndex];
		std::vector<glm::vec3> read(accessor.count);
		fastgltf::iterateAccessorWithIndex<glm::vec3>(
		    asset, accessor, [&](glm::vec3 normal, std::size_t index) { read[index] = normal; });
		return read;
	};
	const auto expected = normals(one->Asset(), one->Asset().meshes[0].primitives[0]);
	const auto &primitives = many->Asset().meshes[0].primitives;
	REQUIRE(primitives.size() == kPrimitives);
	float farthest = 0.0f;
	for (const auto &primitive : primitives)
	{
		const auto read = normals(many->Asset(), primitive);
		REQUIRE(read.size() == expected.size());
		for (std::size_t vertex = 0; vertex < read.size(); ++vertex)
			farthest = std::max(farthest, glm::length(read[vertex] - expected[vertex]));
	}
	CHECK(farthest < 1e-4f);
}


TEST_CASE("Compressed views naming the same bytes of a stream are refused before any is decoded")
{
	// overlap.gltf: four views of 256 MiB in one fallback, each naming the same 256 KiB of stream,
	// which made the reader allocate 1 GiB before the stream failed to decode.
	std::string views;
	for (int view = 0; view < 4; ++view)
		views += std::string{view == 0 ? "" : ","} + R"({"buffer":1,"byteOffset":)" +
		         std::to_string(view * 268435456) +
		         R"(,"byteLength":268435456,"byteStride":4,"extensions":{"EXT_meshopt_compression":)"
		         R"({"buffer":0,"byteLength":262146,"byteStride":4,"count":67108864,)"
		         R"("mode":"ATTRIBUTES"}}})";
	const std::string overlap =
	    R"({"asset":{"version":"2.0"},"extensionsUsed":["EXT_meshopt_compression"],)"
	    R"("extensionsRequired":["EXT_meshopt_compression"],"scenes":[{"nodes":[]}],)"
	    R"("buffers":[{"byteLength":262146,"uri":"data:application/octet-stream;base64,)" +
	    std::string(349528, 'A') +
	    R"("},{"byteLength":1073741824,"extensions":{"EXT_meshopt_compression":{"fallback":true}}}],)"
	    R"("bufferViews":[)" +
	    views + "]}";
	CHECK_THROWS_WITH_AS(
	    Geometry::GltfFile::Parse(AsBytes(overlap), "overlap.gltf"),
	    doctest::Contains("name the same bytes of a stream, which the cook never writes: overlap.gltf"),
	    std::runtime_error);
}
