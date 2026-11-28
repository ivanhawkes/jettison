#include <jettison/geometry/Cook.h>

#include "Extras.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <glm/glm.hpp>
#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include <jettison/geometry/GltfFile.h>
#include <jettison/math/Tangents.h>

namespace Jettison::Geometry
{
namespace
{
// glTF aligns an accessor to its component's size; 4 bytes is the largest the cook stores.
constexpr std::size_t kAlignment = 4;

// The binary chunk, which holds every view's stored bytes, compressed or not.
constexpr std::size_t kBinBuffer = 0;

// The fallback buffer that compressed views decode into: it has a length and no bytes.
constexpr std::size_t kFallbackBuffer = 1;

// EXT_meshopt_compression's vertex codec is version 0; version 1 is KHR_meshopt_compression's, which
// fastgltf 1.0.1 cannot write. Version 0 has one compression level, so the level passed is unused.
constexpr int kVertexCodecVersion = 0;

constexpr int kVertexCodecLevel = 2;

// A level that keeps more than this share of the last level's triangles costs its memory and saves
// little drawing, so it ends the levels.
constexpr double kLeastReduction = 0.9;

// 16-bit indices reach this many vertices.
constexpr std::size_t kShortIndexVertices = std::size_t{std::numeric_limits<std::uint16_t>::max()} + 1;

constexpr float kSnorm16 = 32767.0f;

constexpr float kUnorm16 = 65535.0f;

constexpr float kMaxByteJoint = 255.0f;

constexpr std::string_view kGenerator = "Jettison's cook (jettison_cook)";

constexpr std::array kCookExtensions{
    std::string_view{"EXT_meshopt_compression"}, std::string_view{"KHR_mesh_quantization"}};

std::runtime_error Error(const GltfFile &source, std::string_view what)
{
	return std::runtime_error(std::format("Cannot cook {}: {}", source.Name(), what));
}


std::size_t AlignUp(std::size_t value)
{
	return (value + kAlignment - 1) / kAlignment * kAlignment;
}


// A vertex attribute as float components, whatever its stored type: the unit the cook works in. A
// joint index is a float exactly, as glTF's are at most 16 bits.
struct Stream
{
	std::string name;
	std::uint32_t components{};
	std::vector<float> values;
};

struct Primitive
{
	const fastgltf::Primitive *pSource{};

	// POSITION first, which the simplifier reads, then the source's others in its order.
	std::vector<Stream> streams;
	std::vector<std::uint32_t> indices;
	std::size_t vertexCount{};
};

// One level's indices per primitive, into the finest level's vertices, and its error.
struct Level
{
	std::vector<std::vector<std::uint32_t>> indices;
	float error{};
};


Stream *FindStream(std::vector<Stream> &streams, std::string_view name)
{
	const auto found = std::ranges::find(streams, name, &Stream::name);
	return found == streams.end() ? nullptr : &*found;
}


std::uint32_t Components(const GltfFile &source, fastgltf::AccessorType type)
{
	const auto components = static_cast<std::uint32_t>(fastgltf::getNumComponents(type));
	const bool matrix = type == fastgltf::AccessorType::Mat2 || type == fastgltf::AccessorType::Mat3;
	if (matrix)
		throw Error(source, "an accessor is a 2x2 or 3x3 matrix, which the cook does not copy");
	return components;
}


template <typename Element>
void ReadElements(
    const fastgltf::Asset &asset, const fastgltf::Accessor &accessor, std::vector<float> &values)
{
	fastgltf::iterateAccessorWithIndex<Element>(
	    asset, accessor,
	    [&](const Element &element, std::size_t index)
	    {
		    std::memcpy(
		        values.data() + (index * sizeof(Element) / sizeof(float)), &element, sizeof(Element));
	    });
}


std::vector<float> ReadFloats(const GltfFile &source, const fastgltf::Accessor &accessor)
{
	const auto &asset = source.Asset();
	const std::uint32_t components = Components(source, accessor.type);
	std::vector<float> values(accessor.count * components);
	switch (components)
	{
		case 1: ReadElements<float>(asset, accessor, values); break;
		case 2: ReadElements<glm::vec2>(asset, accessor, values); break;
		case 3: ReadElements<glm::vec3>(asset, accessor, values); break;
		case 4: ReadElements<glm::vec4>(asset, accessor, values); break;
		default: ReadElements<glm::mat4>(asset, accessor, values); break;
	}
	return values;
}


Primitive ReadPrimitive(const GltfFile &source, const fastgltf::Primitive &primitive)
{
	const auto &asset = source.Asset();
	if (primitive.type != fastgltf::PrimitiveType::Triangles)
		throw Error(source, "a primitive is not triangles");
	if (!primitive.targets.empty())
		throw Error(source, "a primitive has morph targets, which the cook does not keep yet");
	const auto position = primitive.findAttribute("POSITION");
	if (position == primitive.attributes.end())
		throw Error(source, "a primitive has no POSITION");

	Primitive read;
	read.pSource = &primitive;
	read.vertexCount = asset.accessors.at(position->accessorIndex).count;
	std::vector<const fastgltf::Attribute *> attributes{&*position};
	for (const auto &attribute : primitive.attributes)
		if (&attribute != &*position)
			attributes.push_back(&attribute);
	for (const fastgltf::Attribute *pAttribute : attributes)
	{
		const auto &accessor = asset.accessors.at(pAttribute->accessorIndex);
		if (accessor.count != read.vertexCount)
			throw Error(
			    source, std::format(
			                "attribute {} has {} values for {} vertices", pAttribute->name,
			                accessor.count, read.vertexCount));
		const std::uint32_t components = Components(source, accessor.type);
		if (components > 4)
			throw Error(source, std::format("attribute {} is a matrix", pAttribute->name));
		read.streams.push_back({pAttribute->name, components, ReadFloats(source, accessor)});
	}

	if (primitive.indicesAccessor)
	{
		const auto &accessor = asset.accessors.at(*primitive.indicesAccessor);
		read.indices.reserve(accessor.count);
		fastgltf::iterateAccessor<std::uint32_t>(
		    asset, accessor, [&](std::uint32_t index) { read.indices.push_back(index); });
		if (std::ranges::any_of(
		        read.indices, [&](std::uint32_t index) { return index >= read.vertexCount; }))
			throw Error(source, "an index is out of its primitive's vertices");
	}
	else
	{
		read.indices.resize(read.vertexCount);
		std::iota(read.indices.begin(), read.indices.end(), 0u);
	}
	if (read.indices.size() % 3 != 0)
		throw Error(source, "a primitive's indices are not whole triangles");
	return read;
}


glm::vec3 Vec3At(const Stream &stream, std::size_t vertex)
{
	return {
	    stream.values[vertex * 3], stream.values[(vertex * 3) + 1], stream.values[(vertex * 3) + 2]};
}


// The renderer's normals for a primitive with none (render's ModelGltf.cpp): each vertex's
// triangles' face normals summed, so a cooked and an uncooked file shade alike.
void AddNormals(Primitive &primitive)
{
	const Stream &positions = primitive.streams.front();
	std::vector<glm::vec3> normals(primitive.vertexCount, glm::vec3{0.0f});
	for (std::size_t index = 0; index + 2 < primitive.indices.size(); index += 3)
	{
		const std::uint32_t a = primitive.indices[index];
		const std::uint32_t b = primitive.indices[index + 1];
		const std::uint32_t c = primitive.indices[index + 2];
		const glm::vec3 face = glm::cross(
		    Vec3At(positions, b) - Vec3At(positions, a), Vec3At(positions, c) - Vec3At(positions, a));
		normals[a] += face;
		normals[b] += face;
		normals[c] += face;
	}
	Stream stream{"NORMAL", 3, std::vector<float>(primitive.vertexCount * 3)};
	for (std::size_t vertex = 0; vertex < primitive.vertexCount; ++vertex)
	{
		const glm::vec3 normal = glm::dot(normals[vertex], normals[vertex]) > 0.0f
		                             ? glm::normalize(normals[vertex])
		                             : glm::vec3{0, 0, 1};
		std::memcpy(stream.values.data() + (vertex * 3), &normal, sizeof normal);
	}
	primitive.streams.push_back(std::move(stream));
}


void AddTangents(const GltfFile &source, Primitive &primitive)
{
	Stream *pNormals = FindStream(primitive.streams, "NORMAL");
	Stream *pTexCoords = FindStream(primitive.streams, "TEXCOORD_0");
	if (FindStream(primitive.streams, "TANGENT") || !pNormals || !pTexCoords ||
	    pTexCoords->components != 2)
		return;

	const auto asVectors = []<typename Vector>(const Stream &stream, std::type_identity<Vector>)
	{
		std::vector<Vector> vectors(stream.values.size() / stream.components);
		std::memcpy(vectors.data(), stream.values.data(), stream.values.size() * sizeof(float));
		return vectors;
	};
	const auto positions = asVectors(primitive.streams.front(), std::type_identity<glm::vec3>{});
	const auto normals = asVectors(*pNormals, std::type_identity<glm::vec3>{});
	const auto texCoords = asVectors(*pTexCoords, std::type_identity<glm::vec2>{});
	Math::TangentMesh tangents;
	try
	{
		tangents = Math::GenerateTangents(positions, normals, texCoords, primitive.indices);
	}
	catch (const std::invalid_argument &error)
	{
		throw Error(source, error.what());
	}

	for (Stream &stream : primitive.streams)
	{
		std::vector<float> values(tangents.sourceVertices.size() * stream.components);
		for (std::size_t vertex = 0; vertex < tangents.sourceVertices.size(); ++vertex)
			std::memcpy(
			    values.data() + (vertex * stream.components),
			    stream.values.data() +
			        (std::size_t{tangents.sourceVertices[vertex]} * stream.components),
			    stream.components * sizeof(float));
		stream.values = std::move(values);
	}
	Stream stream{"TANGENT", 4, std::vector<float>(tangents.tangents.size() * 4)};
	std::memcpy(stream.values.data(), tangents.tangents.data(), stream.values.size() * sizeof(float));
	primitive.streams.push_back(std::move(stream));
	primitive.indices = std::move(tangents.indices);
	primitive.vertexCount = tangents.sourceVertices.size();
}


// Triangles in vertex-cache order and vertices in the order they are first fetched, unused ones
// dropped.
void Optimise(Primitive &primitive)
{
	std::vector<std::uint32_t> ordered(primitive.indices.size());
	meshopt_optimizeVertexCache(
	    ordered.data(), primitive.indices.data(), primitive.indices.size(), primitive.vertexCount);
	std::vector<std::uint32_t> remap(primitive.vertexCount);
	const std::size_t used = meshopt_optimizeVertexFetchRemap(
	    remap.data(), ordered.data(), ordered.size(), primitive.vertexCount);
	meshopt_remapIndexBuffer(primitive.indices.data(), ordered.data(), ordered.size(), remap.data());
	for (Stream &stream : primitive.streams)
	{
		std::vector<float> values(used * stream.components);
		for (std::size_t vertex = 0; vertex < primitive.vertexCount; ++vertex)
			if (remap[vertex] != ~0u)
				std::memcpy(
				    values.data() + (std::size_t{remap[vertex]} * stream.components),
				    stream.values.data() + (vertex * stream.components),
				    stream.components * sizeof(float));
		stream.values = std::move(values);
	}
	primitive.vertexCount = used;
}


// Each level simplified from the finest, so its error is measured against the finest, with every
// primitive's border locked, so primitives that meet stay joined at every level.
std::vector<Level> BuildLevels(const std::vector<Primitive> &primitives, const LodSettings &settings)
{
	std::vector<float> positions;
	for (const Primitive &primitive : primitives)
		positions.insert(
		    positions.end(), primitive.streams.front().values.begin(),
		    primitive.streams.front().values.end());
	const float scale =
	    meshopt_simplifyScale(positions.data(), positions.size() / 3, sizeof(float) * 3);
	const float targetError = settings.maxError * scale;

	const auto triangles = [](const Level &level)
	{
		std::size_t count = 0;
		for (const auto &indices : level.indices)
			count += indices.size() / 3;
		return count;
	};
	std::vector<Level> levels(1);
	for (const Primitive &primitive : primitives)
		levels.front().indices.push_back(primitive.indices);

	for (std::uint32_t levelIndex = 1; levelIndex < settings.maxLevels; ++levelIndex)
	{
		const Level &previous = levels.back();
		Level level{{}, previous.error};
		const double share = std::pow(double{settings.reduction}, static_cast<double>(levelIndex));
		for (std::size_t index = 0; index < primitives.size(); ++index)
		{
			const Primitive &primitive = primitives[index];
			const std::size_t finestTriangles = primitive.indices.size() / 3;
			const auto target =
			    static_cast<std::size_t>(static_cast<double>(finestTriangles) * share) * 3;
			std::vector<std::uint32_t> simplified(primitive.indices.size());
			float error = 0.0f;
			simplified.resize(meshopt_simplify(
			    simplified.data(), primitive.indices.data(), primitive.indices.size(),
			    primitive.streams.front().values.data(), primitive.vertexCount, sizeof(float) * 3,
			    target, targetError, meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute,
			    &error));

			// A primitive simplified away would leave its material with no triangles, so it keeps the
			// last level's.
			if (simplified.empty())
			{
				level.indices.push_back(previous.indices[index]);
				continue;
			}
			std::vector<std::uint32_t> ordered(simplified.size());
			meshopt_optimizeVertexCache(
			    ordered.data(), simplified.data(), simplified.size(), primitive.vertexCount);
			level.indices.push_back(std::move(ordered));
			level.error = std::max(level.error, error);
		}
		if (static_cast<double>(triangles(level)) >
		    kLeastReduction * static_cast<double>(triangles(previous)))
			break;
		levels.push_back(std::move(level));
	}
	return levels;
}


std::string JsonString(std::string_view text)
{
	std::string quoted{"\""};
	for (const char character : text)
	{
		if (character == '"' || character == '\\')
			quoted += std::format("\\{}", character);
		else if (static_cast<unsigned char>(character) < 0x20)
			quoted += std::format("\\u{:04x}", static_cast<unsigned>(character));
		else
			quoted += character;
	}
	return quoted + '"';
}


// What fastgltf's exporter asks for as it writes each object's extras.
struct WrittenExtras
{
	std::vector<std::string> meshes;
	std::size_t scene{};
	std::string sceneExtras;
};


std::optional<std::string> WriteExtras(std::size_t index, fastgltf::Category category, void *pUser)
{
	const auto &extras = *static_cast<const WrittenExtras *>(pUser);
	if (category == fastgltf::Category::Meshes && index < extras.meshes.size())
		return extras.meshes[index];
	if (category == fastgltf::Category::Scenes && index == extras.scene)
		return extras.sceneExtras;
	return std::nullopt;
}


// The output asset's buffers as the cook fills them.
class Writer
{
  public:
	explicit Writer(fastgltf::Asset &asset) : m_asset{asset}
	{
	}

	std::size_t AddView(
	    std::span<const std::byte> bytes, std::optional<std::size_t> stride = {},
	    std::optional<fastgltf::BufferTarget> target = {})
	{
		m_bin.resize(AlignUp(m_bin.size()));
		fastgltf::BufferView view{};
		view.bufferIndex = kBinBuffer;
		view.byteOffset = m_bin.size();
		view.byteLength = bytes.size();
		if (stride)
			view.byteStride = *stride;
		if (target)
			view.target = *target;
		m_bin.insert(m_bin.end(), bytes.begin(), bytes.end());
		m_asset.bufferViews.push_back(std::move(view));
		return m_asset.bufferViews.size() - 1;
	}

	// encoded is the codec's output, which decodes into count elements of stride bytes.
	std::size_t AddCompressedView(
	    std::span<const unsigned char> encoded, std::size_t count, std::size_t stride,
	    fastgltf::MeshoptCompressionMode mode, fastgltf::BufferTarget target)
	{
		m_bin.resize(AlignUp(m_bin.size()));
		m_fallbackLength = AlignUp(m_fallbackLength);
		fastgltf::BufferView view{};
		view.bufferIndex = kFallbackBuffer;
		view.byteOffset = m_fallbackLength;
		view.byteLength = count * stride;
		if (mode == fastgltf::MeshoptCompressionMode::Attributes)
			view.byteStride = stride;
		view.target = target;
		view.meshoptCompression = fastgltf::CompressedBufferView{
		    kBinBuffer, m_bin.size(), encoded.size(),
		    count,      mode,         fastgltf::MeshoptCompressionFilter::None,
		    stride};
		const auto *pEncoded = reinterpret_cast<const std::byte *>(encoded.data());
		m_bin.insert(m_bin.end(), pEncoded, pEncoded + encoded.size());
		m_fallbackLength += view.byteLength;
		m_asset.bufferViews.push_back(std::move(view));
		return m_asset.bufferViews.size() - 1;
	}

	void Finish()
	{
		m_bin.resize(AlignUp(m_bin.size()));
		fastgltf::Buffer bin{};
		bin.byteLength = m_bin.size();
		bin.data = fastgltf::sources::Vector{std::move(m_bin)};
		m_asset.buffers.push_back(std::move(bin));
		if (m_fallbackLength == 0)
			return;
		fastgltf::Buffer fallback{};
		fallback.byteLength = m_fallbackLength;
		fallback.data = fastgltf::sources::Fallback{};
		m_asset.buffers.push_back(std::move(fallback));
	}

  private:
	fastgltf::Asset &m_asset;
	std::vector<std::byte> m_bin;
	std::size_t m_fallbackLength{};
};


fastgltf::AccessorType VectorType(std::uint32_t components)
{
	switch (components)
	{
		case 1: return fastgltf::AccessorType::Scalar;
		case 2: return fastgltf::AccessorType::Vec2;
		case 3: return fastgltf::AccessorType::Vec3;
		case 4: return fastgltf::AccessorType::Vec4;
		default: return fastgltf::AccessorType::Mat4;
	}
}


std::size_t AddAccessor(
    fastgltf::Asset &asset, std::size_t view, std::size_t count, fastgltf::AccessorType type,
    fastgltf::ComponentType componentType, bool normalized)
{
	fastgltf::Accessor accessor{};
	accessor.bufferViewIndex = view;
	accessor.count = count;
	accessor.type = type;
	accessor.componentType = componentType;
	accessor.normalized = normalized;
	asset.accessors.push_back(std::move(accessor));
	return asset.accessors.size() - 1;
}


void SetBounds(fastgltf::Accessor &accessor, std::span<const float> values, std::uint32_t components)
{
	auto min = fastgltf::AccessorBoundsArray::ForType<double>(components);
	auto max = fastgltf::AccessorBoundsArray::ForType<double>(components);
	for (std::uint32_t component = 0; component < components; ++component)
	{
		double low = std::numeric_limits<double>::infinity();
		double high = -low;
		for (std::size_t index = component; index < values.size(); index += components)
		{
			low = std::min(low, double{values[index]});
			high = std::max(high, double{values[index]});
		}
		min.data<double>()[component] = low;
		max.data<double>()[component] = high;
	}
	accessor.min = std::move(min);
	accessor.max = std::move(max);
}


// An accessor that is not a vertex's (a skin's matrices, a clip's times and values) as plain floats.
std::size_t CopyAccessor(
    const GltfFile &source, fastgltf::Asset &out, Writer &writer, std::size_t index)
{
	const auto &accessor = source.Asset().accessors.at(index);
	const std::vector<float> values = ReadFloats(source, accessor);
	const std::uint32_t components = Components(source, accessor.type);
	const std::size_t view = writer.AddView(std::as_bytes(std::span{values}));
	const std::size_t copied =
	    AddAccessor(out, view, accessor.count, accessor.type, fastgltf::ComponentType::Float, false);
	SetBounds(out.accessors[copied], values, components);
	out.accessors[copied].name = accessor.name;
	return copied;
}


// A vertex stream as it is stored: its layout, padded to whole 4-byte words as glTF and the vertex
// codec both want, and its quantised bytes.
struct StoredStream
{
	fastgltf::ComponentType componentType{fastgltf::ComponentType::Float};
	bool normalized{false};
	std::size_t stride{};
	std::vector<unsigned char> bytes;
};


template <typename Component>
StoredStream Quantise(
    const Stream &stream, std::size_t vertexCount, fastgltf::ComponentType componentType,
    bool normalized, auto convert)
{
	StoredStream stored{componentType, normalized, AlignUp(sizeof(Component) * stream.components), {}};
	stored.bytes.resize(stored.stride * vertexCount);
	for (std::size_t vertex = 0; vertex < vertexCount; ++vertex)
		for (std::uint32_t component = 0; component < stream.components; ++component)
		{
			const Component value = convert(stream.values[(vertex * stream.components) + component]);
			std::memcpy(
			    stored.bytes.data() + (vertex * stored.stride) + (component * sizeof(Component)),
			    &value, sizeof value);
		}
	return stored;
}


// KHR_mesh_quantization's types by attribute: unit vectors and weights in 16 bits normalised,
// texture coordinates in 16 bits where they lie within [0, 1] (tiling ones stay float), joints in 8
// bits where they fit; positions, and any attribute the cook does not know, stay float.
StoredStream Store(const Stream &stream, std::size_t vertexCount)
{
	const auto within = [&](float low, float high)
	{
		return std::ranges::all_of(
		    stream.values, [&](float value) { return value >= low && value <= high; });
	};
	const auto snorm16 = [](float value)
	{ return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * kSnorm16)); };
	const auto unorm16 = [](float value)
	{ return static_cast<std::uint16_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * kUnorm16)); };
	const std::string_view name = stream.name;

	if (name == "NORMAL" || name == "TANGENT")
		return Quantise<std::int16_t>(
		    stream, vertexCount, fastgltf::ComponentType::Short, true, snorm16);
	if (name.starts_with("WEIGHTS_") || name.starts_with("COLOR_") ||
	    (name.starts_with("TEXCOORD_") && within(0.0f, 1.0f)))
		return Quantise<std::uint16_t>(
		    stream, vertexCount, fastgltf::ComponentType::UnsignedShort, true, unorm16);
	if (name.starts_with("JOINTS_"))
	{
		if (within(0.0f, kMaxByteJoint))
			return Quantise<std::uint8_t>(
			    stream, vertexCount, fastgltf::ComponentType::UnsignedByte, false,
			    [](float value) { return static_cast<std::uint8_t>(value); });
		return Quantise<std::uint16_t>(
		    stream, vertexCount, fastgltf::ComponentType::UnsignedShort, false,
		    [](float value) { return static_cast<std::uint16_t>(value); });
	}
	return Quantise<float>(
	    stream, vertexCount, fastgltf::ComponentType::Float, false, [](float value) { return value; });
}


std::size_t AddVertexStream(
    fastgltf::Asset &out, Writer &writer, const Stream &stream, std::size_t vertexCount)
{
	const StoredStream stored = Store(stream, vertexCount);
	std::vector<unsigned char> encoded(meshopt_encodeVertexBufferBound(vertexCount, stored.stride));
	encoded.resize(meshopt_encodeVertexBufferLevel(
	    encoded.data(), encoded.size(), stored.bytes.data(), vertexCount, stored.stride,
	    kVertexCodecLevel, kVertexCodecVersion));
	const std::size_t view = writer.AddCompressedView(
	    encoded, vertexCount, stored.stride, fastgltf::MeshoptCompressionMode::Attributes,
	    fastgltf::BufferTarget::ArrayBuffer);
	const std::size_t accessor = AddAccessor(
	    out, view, vertexCount, VectorType(stream.components), stored.componentType, stored.normalized);
	if (stream.name == "POSITION")
		SetBounds(out.accessors[accessor], stream.values, stream.components);
	return accessor;
}


std::size_t AddIndices(
    fastgltf::Asset &out, Writer &writer, std::span<const std::uint32_t> indices,
    std::size_t vertexCount)
{
	const bool shortIndices = vertexCount <= kShortIndexVertices;
	std::vector<unsigned char> encoded(meshopt_encodeIndexBufferBound(indices.size(), vertexCount));
	encoded.resize(
	    meshopt_encodeIndexBuffer(encoded.data(), encoded.size(), indices.data(), indices.size()));
	const std::size_t view = writer.AddCompressedView(
	    encoded, indices.size(), shortIndices ? sizeof(std::uint16_t) : sizeof(std::uint32_t),
	    fastgltf::MeshoptCompressionMode::Triangles, fastgltf::BufferTarget::ElementArrayBuffer);
	return AddAccessor(
	    out, view, indices.size(), fastgltf::AccessorType::Scalar,
	    shortIndices ? fastgltf::ComponentType::UnsignedShort : fastgltf::ComponentType::UnsignedInt,
	    false);
}


std::string LevelsExtras(const std::vector<Level> &levels)
{
	std::string json = std::format("{{\"{}\":{{\"{}\":[", kExtrasObject, kLevelsName);
	for (std::size_t index = 0; index < levels.size(); ++index)
		json += std::format(
		    "{}{{\"{}\":{},\"{}\":{}}}", index == 0 ? "" : ",", kPrimitivesName,
		    levels[index].indices.size(), kErrorName, levels[index].error);
	return json + "]}}";
}


fastgltf::Mesh CookMesh(
    const GltfFile &source, const fastgltf::Mesh &mesh, const LodSettings &settings,
    fastgltf::Asset &out, Writer &writer, std::string &extras)
{
	std::vector<Primitive> primitives;
	for (const auto &primitive : mesh.primitives)
	{
		Primitive read = ReadPrimitive(source, primitive);
		if (!FindStream(read.streams, "NORMAL"))
			AddNormals(read);
		AddTangents(source, read);
		Optimise(read);
		primitives.push_back(std::move(read));
	}
	const std::vector<Level> levels = BuildLevels(primitives, settings);

	std::vector<std::vector<fastgltf::Attribute>> attributes;
	for (const Primitive &primitive : primitives)
	{
		auto &written = attributes.emplace_back();
		for (const Stream &stream : primitive.streams)
			written.push_back(
			    {stream.name, AddVertexStream(out, writer, stream, primitive.vertexCount)});
	}

	fastgltf::Mesh cooked{};
	cooked.name = mesh.name;
	for (const Level &level : levels)
		for (std::size_t index = 0; index < primitives.size(); ++index)
		{
			fastgltf::Primitive primitive{};
			primitive.type = fastgltf::PrimitiveType::Triangles;
			primitive.attributes = attributes[index];
			primitive.materialIndex = primitives[index].pSource->materialIndex;
			primitive.mappings = primitives[index].pSource->mappings;
			primitive.indicesAccessor =
			    AddIndices(out, writer, level.indices[index], primitives[index].vertexCount);
			cooked.primitives.push_back(std::move(primitive));
		}
	extras = LevelsExtras(levels);
	return cooked;
}


fastgltf::sources::BufferView CopyImage(
    const GltfFile &source, const fastgltf::Image &image, Writer &writer)
{
	const auto &asset = source.Asset();
	return std::visit(
	    [&](const auto &data) -> fastgltf::sources::BufferView
	    {
		    using Data = std::decay_t<decltype(data)>;
		    if constexpr (std::is_same_v<Data, fastgltf::sources::BufferView>)
			    return {
			        writer.AddView(fastgltf::DefaultBufferDataAdapter{}(asset, data.bufferViewIndex)),
			        data.mimeType};
		    else if constexpr (std::is_same_v<Data, fastgltf::sources::Array>)
			    return {writer.AddView({data.bytes.data(), data.bytes.size_bytes()}), data.mimeType};
		    else if constexpr (std::is_same_v<Data, fastgltf::sources::Vector>)
			    return {writer.AddView(data.bytes), data.mimeType};
		    else
			    throw Error(source, std::format("image '{}' is not embedded", image.name));
	    },
	    image.data);
}


void AddExtensions(std::vector<std::string> &extensions)
{
	for (const std::string_view extension : kCookExtensions)
		if (std::ranges::find(extensions, extension) == extensions.end())
			extensions.emplace_back(extension);
}
} // namespace


std::vector<std::byte> CookModel(
    const GltfFile &source, const LodSettings &settings, std::string_view cookKey)
{
	// Written so a NaN is out of range too.
	const bool reductionInRange = settings.reduction > 0.0f && settings.reduction < 1.0f;
	const bool errorInRange = settings.maxError >= 0.0f;
	if (settings.maxLevels < 1 || !reductionInRange || !errorInRange)
		throw Error(source, "its level-of-detail settings are out of range");
	const fastgltf::Asset &asset = source.Asset();
	if (asset.scenes.empty())
		throw Error(source, "it has no scene, whose extras carry the cook key");

	fastgltf::Asset out;
	out.assetInfo = asset.assetInfo;
	out.assetInfo.generator = kGenerator;
	out.extensionsUsed = asset.extensionsUsed;
	out.extensionsRequired = asset.extensionsRequired;
	AddExtensions(out.extensionsUsed);
	AddExtensions(out.extensionsRequired);
	out.defaultScene = asset.defaultScene;
	out.cameras = asset.cameras;
	out.lights = asset.lights;
	out.materials = asset.materials;
	out.nodes = asset.nodes;
	out.samplers = asset.samplers;
	out.scenes = asset.scenes;
	out.textures = asset.textures;
	out.materialVariants = asset.materialVariants;
	Writer writer{out};

	WrittenExtras extras;
	extras.scene = asset.defaultScene.value_or(0);
	extras.sceneExtras =
	    std::format("{{\"{}\":{{\"{}\":{}}}}}", kExtrasObject, kCookKeyName, JsonString(cookKey));
	extras.meshes.resize(asset.meshes.size());
	for (std::size_t index = 0; index < asset.meshes.size(); ++index)
		out.meshes.push_back(
		    CookMesh(source, asset.meshes[index], settings, out, writer, extras.meshes[index]));

	// A clip's times are often shared by its samplers, so each accessor is copied once.
	std::map<std::size_t, std::size_t> copied;
	const auto copy = [&](std::size_t index)
	{
		const auto [found, inserted] = copied.try_emplace(index);
		if (inserted)
			found->second = CopyAccessor(source, out, writer, index);
		return found->second;
	};
	out.skins = asset.skins;
	for (auto &skin : out.skins)
		if (skin.inverseBindMatrices)
			skin.inverseBindMatrices = copy(*skin.inverseBindMatrices);
	out.animations = asset.animations;
	for (auto &animation : out.animations)
		for (auto &sampler : animation.samplers)
		{
			sampler.inputAccessor = copy(sampler.inputAccessor);
			sampler.outputAccessor = copy(sampler.outputAccessor);
		}
	for (const auto &image : asset.images)
		out.images.push_back({CopyImage(source, image, writer), image.name});
	writer.Finish();

	fastgltf::Exporter exporter;
	exporter.setExtrasWriteCallback(&WriteExtras);
	exporter.setUserPointer(&extras);
	auto written = exporter.writeGltfBinary(out);
	if (written.error() != fastgltf::Error::None)
		throw Error(
		    source,
		    std::format(
		        "fastgltf could not write it ({})", fastgltf::getErrorMessage(written.error())));
	return std::move(written.get().output);
}
} // namespace Jettison::Geometry
