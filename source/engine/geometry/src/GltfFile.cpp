#include <jettison/geometry/GltfFile.h>

#include "Extras.h"

#include <fastgltf/core.hpp>
#include <meshoptimizer.h>
#include <simdjson.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <map>
#include <numeric>
#include <span>
#include <stdexcept>
#include <system_error>
#include <tuple>
#include <utility>
#include <variant>

namespace Jettison::Geometry
{
namespace
{
// What a cooked file uses (Cook.h); a file that does not use them parses the same. The parser fills
// only these extensions' fields, so one added here whose indices it fills needs them in CheckIndices.
constexpr auto kExtensions = fastgltf::Extensions::EXT_meshopt_compression |
                             fastgltf::Extensions::KHR_mesh_quantization |
                             fastgltf::Extensions::KHR_texture_basisu;

// EXT_meshopt_compression's limits on an attribute view's stride, which meshoptimizer asserts rather
// than checks.
constexpr std::size_t kAttributeStrideAlignment = 4;

constexpr std::size_t kMaxAttributeStride = 256;

constexpr std::size_t kTriangleCorners = 3;

// The most one compressed view decodes to, one attribute or one level's indices of one primitive, so
// a file cannot make the reader allocate more for it: 22 million float positions, or 22 million
// triangles' 32-bit indices, about 540 times the largest view the test content cooks to (496,200
// bytes, the large asteroid's 81,920 triangles, measured 2026-11-28).
constexpr std::size_t kMaxDecodedViewBytes = std::size_t{256} * 1024 * 1024;

// The most a file's fallback buffers decode to together, each to its furthest view's end, so neither
// a view's offset nor many buffers can make the reader allocate more: four views at their limit, 470
// times the large asteroid's buffer (2,275,520 bytes). A 2 KB file of four fallbacks at this size once
// made the reader allocate 4 GiB.
constexpr std::size_t kMaxDecodedFileBytes = 4 * kMaxDecodedViewBytes;

// The most a .gltf's buffer files are read for together, as much as its fallbacks may decode to, so a
// .gltf's buffers cost a reader no more than a .glb's. Sixteen buffers naming one 64 MiB file once
// read it sixteen times, whole, for 2 GiB.
constexpr std::size_t kMaxBufferFileBytes = kMaxDecodedFileBytes;

// The read limits are a decoded view's worth: float positions, or 32-bit indices.
static_assert(kMaxReadVertices == kMaxDecodedViewBytes / sizeof(std::array<float, 3>));

static_assert(kMaxReadIndices == kMaxDecodedViewBytes / sizeof(std::uint32_t));

// The most keys a file's clips give the rig reader together, each channel's sampler's times and
// values counted, since it reads them for each channel: as many of its four-float values as a
// decoded view at its limit holds. A 16 MB file of one sampler of a million keys named by 128
// channels once made it take 2.5 GiB (anim.py); arm.glb's clip has 318.
constexpr std::size_t kMaxReadKeys = kMaxDecodedViewBytes / sizeof(std::array<float, 4>);

// meshoptimizer 1.3's vertex codec (vertexcodec.cpp), whose decoder refuses a version past 1
// (kDecodeVertexVersion, lines 126 and 1836): version 1 reads a control byte for each 4 bytes of a
// vertex (decodeVertexBlock, 719 and 728) in every block of at most 256 vertices (kVertexBlockMaxSize,
// 129), and a control byte can say its bytes are all zero (743), so a byte of a stream decodes to
// 1,024 at the most; version 0's decodes to 64, a header byte for every 64 of a block's (648).
constexpr std::size_t kMeshoptVertexBlock = 256;

constexpr std::size_t kMeshoptBytesPerControl = 4;

constexpr std::size_t kMostDecodedPerAttributeByte = kMeshoptVertexBlock * kMeshoptBytesPerControl;

// The bytes meshoptimizer 1.3's index decoders need beside a byte for each triangle, or each index, of
// a stream, their first checks (indexcodec.cpp, lines 392 and 652): a triangle stream's header and 16
// bytes of its table, a sequence's header and 4 bytes of tail.
constexpr std::size_t kTriangleStreamFixedBytes = 1 + 16;

constexpr std::size_t kSequenceStreamFixedBytes = 1 + 4;


bool IsGltf(std::string_view path)
{
	const auto dot = path.rfind('.');
	if (dot == std::string_view::npos)
		return false;
	std::string extension{path.substr(dot)};
	std::ranges::transform(
	    extension, extension.begin(),
	    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return extension == ".gltf" || extension == ".glb";
}


// The extras as parsed; fastgltf's callback cannot throw through the parser, so a malformed one is
// noted and refused once it returns.
struct ParsedExtras
{
	std::vector<std::pair<std::size_t, std::vector<MeshLevel>>> levels;
	std::string cookKey;
	bool malformed{false};
};


bool ReadNumber(simdjson::dom::element element, double &value)
{
	if (element.get(value) == simdjson::SUCCESS)
		return true;
	std::int64_t integer{};
	if (element.get(integer) != simdjson::SUCCESS)
		return false;
	value = static_cast<double>(integer);
	return true;
}


void ReadExtras(
    simdjson::dom::object *pExtras, std::size_t index, fastgltf::Category category, void *pUser)
{
	auto &parsed = *static_cast<ParsedExtras *>(pUser);
	simdjson::dom::object ours;
	if ((*pExtras)[kExtrasObject].get(ours) != simdjson::SUCCESS)
		return;
	if (category == fastgltf::Category::Scenes)
	{
		std::string_view key;
		if (ours[kCookKeyName].get(key) == simdjson::SUCCESS)
			parsed.cookKey = key;
		else
			parsed.malformed = true;
		return;
	}
	if (category != fastgltf::Category::Meshes)
		return;
	simdjson::dom::array levels;
	if (ours[kLevelsName].get(levels) != simdjson::SUCCESS || levels.size() == 0)
	{
		parsed.malformed = true;
		return;
	}
	std::vector<MeshLevel> read;
	std::uint32_t first = 0;
	for (const simdjson::dom::element level : levels)
	{
		std::uint64_t count{};
		simdjson::dom::element errorElement;
		double error{};

		// The finest level's error is 0 and none is less than the finer level's, so none is negative,
		// as the cook writes them; each is a float.
		if (level[kPrimitivesName].get(count) != simdjson::SUCCESS ||
		    level[kErrorName].get(errorElement) != simdjson::SUCCESS ||
		    !ReadNumber(errorElement, error) || count == 0 ||
		    count > std::numeric_limits<std::uint32_t>::max() ||
		    (read.empty() ? error != 0.0 : error < double{read.back().error}) ||
		    error > std::numeric_limits<float>::max())
		{
			parsed.malformed = true;
			return;
		}
		read.push_back({first, static_cast<std::uint32_t>(count), static_cast<float>(error)});
		first += static_cast<std::uint32_t>(count);
	}
	parsed.levels.emplace_back(index, std::move(read));
}


std::span<const std::byte> BufferBytes(const fastgltf::Asset &asset, std::size_t bufferIndex)
{
	return std::visit(
	    [](const auto &source) -> std::span<const std::byte>
	    {
		    using Source = std::decay_t<decltype(source)>;
		    if constexpr (std::is_same_v<Source, fastgltf::sources::Array>)
			    return {source.bytes.data(), source.bytes.size_bytes()};
		    else if constexpr (std::is_same_v<Source, fastgltf::sources::Vector>)
			    return {source.bytes.data(), source.bytes.size()};
		    else if constexpr (std::is_same_v<Source, fastgltf::sources::ByteView>)
			    return source.bytes;
		    else
			    return {};
	    },
	    asset.buffers.at(bufferIndex).data);
}


// The first length bytes of a buffer's file, which must hold that many.
std::vector<std::byte> ReadBufferFile(
    const std::filesystem::path &path, std::size_t length, std::string_view uri,
    const std::string &name)
{
	std::error_code error;
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	std::ifstream stream{path, std::ios::binary};
	std::vector<std::byte> bytes;
	if (!error && size >= length)
	{
		bytes.resize(length);
		stream.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(length));
	}
	if (error || size < length || !stream)
		throw std::runtime_error(
		    std::format(
		        "glTF model's buffer '{}' cannot be read, or is shorter than the buffer: {}", uri,
		        name));
	return bytes;
}


// Read here rather than by fastgltf's LoadExternalBuffers, which would follow a URI anywhere. Each
// buffer's file must lie within directory, so a model's URI cannot read what is outside it, and a file
// several buffers name is read once, as far as the longest of them.
void ReadBufferFiles(
    fastgltf::Asset &asset, const std::filesystem::path &directory, const std::string &name)
{
	struct File
	{
		std::string uri;
		std::size_t length{};
		std::size_t buffers{};
		std::vector<std::byte> bytes;
	};

	std::map<std::filesystem::path, File> files;
	std::vector<std::filesystem::path> paths(asset.buffers.size());
	std::size_t declared = 0;
	for (std::size_t index = 0; index < asset.buffers.size(); ++index)
	{
		const fastgltf::Buffer &buffer = asset.buffers[index];
		const auto *pSource = std::get_if<fastgltf::sources::URI>(&buffer.data);
		if (!pSource)
			continue;
		if (directory.empty())
			throw std::runtime_error(
			    "glTF model keeps a buffer in another file, which a resource cannot read: " + name +
			    " (make it a .glb, or embed the buffer)");
		const auto &uri = *pSource->uri;
		const std::optional<std::filesystem::path> path =
		    uri.isLocalPath() ? FileWithin(directory, uri.fspath()) : std::nullopt;
		if (!path)
			throw std::runtime_error(
			    std::format(
			        "glTF model's buffer '{}' is outside its directory: {}", uri.string(), name));
		if (buffer.byteLength > kMaxBufferFileBytes - declared)
			throw std::runtime_error(
			    std::format(
			        "glTF model's buffer files are declared longer than {} bytes together: {}",
			        kMaxBufferFileBytes, name));
		declared += buffer.byteLength;
		paths[index] = *path;
		File &file = files[*path];
		if (file.buffers++ == 0 || buffer.byteLength > file.length)
		{
			file.uri = uri.string();
			file.length = buffer.byteLength;
		}
	}
	for (auto &[path, file] : files)
		file.bytes = ReadBufferFile(path, file.length, file.uri, name);
	for (std::size_t index = 0; index < asset.buffers.size(); ++index)
	{
		if (paths[index].empty())
			continue;
		File &file = files.at(paths[index]);
		const std::size_t length = asset.buffers[index].byteLength;

		// The last buffer to name a file takes its bytes, so a file named once is not copied.
		const std::span<const std::byte> prefix = std::span{file.bytes}.first(length);
		std::vector<std::byte> bytes = --file.buffers == 0
		                                   ? std::move(file.bytes)
		                                   : std::vector<std::byte>{prefix.begin(), prefix.end()};
		bytes.resize(length);
		asset.buffers[index].data = fastgltf::sources::Vector{std::move(bytes)};
	}
}


bool ValidCompression(const fastgltf::CompressedBufferView &compressed)
{
	switch (compressed.mode)
	{
		case fastgltf::MeshoptCompressionMode::Attributes:
			return compressed.byteStride != 0 &&
			       compressed.byteStride % kAttributeStrideAlignment == 0 &&
			       compressed.byteStride <= kMaxAttributeStride;
		case fastgltf::MeshoptCompressionMode::Triangles:
			if (compressed.count % kTriangleCorners != 0)
				return false;
			[[fallthrough]];
		case fastgltf::MeshoptCompressionMode::Indices:
			return compressed.byteStride == sizeof(std::uint16_t) ||
			       compressed.byteStride == sizeof(std::uint32_t);
	}
	return false;
}


// The fewest bytes a valid stream holds for what the view claims it decodes to, which is within
// kMaxDecodedViewBytes, so none of this wraps.
std::size_t LeastCompressedBytes(const fastgltf::CompressedBufferView &compressed)
{
	switch (compressed.mode)
	{
		case fastgltf::MeshoptCompressionMode::Attributes:
		{
			const std::size_t decoded = compressed.count * compressed.byteStride;
			return (decoded + kMostDecodedPerAttributeByte - 1) / kMostDecodedPerAttributeByte;
		}
		case fastgltf::MeshoptCompressionMode::Triangles:
			return kTriangleStreamFixedBytes + (compressed.count / kTriangleCorners);
		case fastgltf::MeshoptCompressionMode::Indices:
			return kSequenceStreamFixedBytes + compressed.count;
	}
	return 0;
}


bool InRange(std::size_t offset, std::size_t length, std::size_t size)
{
	return offset <= size && length <= size - offset;
}


// Each compressed view decoded into its fallback buffer, which then holds the bytes the file's
// accessors read, as an uncompressed file's buffer does. A fallback is made only as long as its views
// reach, whatever length it declares, and only once every view and the total have been checked, so a
// file refused before decoding costs no allocation.
void DecodeCompressedViews(fastgltf::Asset &asset, const std::string &name)
{
	const auto isFallback = [&](std::size_t index)
	{
		return index < asset.buffers.size() &&
		       std::holds_alternative<fastgltf::sources::Fallback>(asset.buffers[index].data);
	};
	const auto tooLarge = [&]
	{
		return std::runtime_error(
		    std::format(
		        "A glTF file's fallback buffers decode to more than {} bytes: {}", kMaxDecodedFileBytes,
		        name));
	};
	std::vector<std::size_t> extents(asset.buffers.size());
	std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> streams;
	for (const auto &view : asset.bufferViews)
	{
		if (!view.meshoptCompression)
			continue;
		const fastgltf::CompressedBufferView &compressed = *view.meshoptCompression;
		if (!isFallback(view.bufferIndex))
			throw std::runtime_error(
			    "A compressed glTF view decodes into a buffer that is not its fallback: " + name);
		if (!ValidCompression(compressed))
			throw std::runtime_error(
			    "A compressed glTF view's stride or count breaks EXT_meshopt_compression: " + name);

		// meshoptimizer writes an index view's indices as numbers of its stride, so they are aligned
		// to it, as glTF aligns any accessor's data to its components; a vertex view to the 4 bytes
		// an attribute's stride is a multiple of. The gltf fuzz target found a view at an odd offset
		// (a misaligned store in meshopt_decodeIndexBuffer, 2026-11-28).
		const std::size_t alignment = compressed.mode == fastgltf::MeshoptCompressionMode::Attributes
		                                  ? kAttributeStrideAlignment
		                                  : compressed.byteStride;
		if (view.byteOffset % alignment != 0)
			throw std::runtime_error(
			    "A compressed glTF view's offset is not a multiple of its numbers' alignment: " + name);
		if (compressed.count > kMaxDecodedViewBytes / compressed.byteStride)
			throw std::runtime_error(
			    std::format(
			        "A compressed glTF view decodes to more than {} bytes: {}", kMaxDecodedViewBytes,
			        name));
		if (compressed.byteLength < LeastCompressedBytes(compressed))
			throw std::runtime_error(
			    std::format(
			        "A compressed glTF view of {} bytes claims more than meshoptimizer's codec decodes "
			        "them to: {}",
			        compressed.byteLength, name));
		if (compressed.count * compressed.byteStride != view.byteLength ||
		    !InRange(view.byteOffset, view.byteLength, asset.buffers[view.bufferIndex].byteLength) ||
		    !InRange(
		        compressed.byteOffset, compressed.byteLength,
		        BufferBytes(asset, compressed.bufferIndex).size()))
			throw std::runtime_error("A compressed glTF view is out of its buffers' range: " + name);
		if (compressed.filter != fastgltf::MeshoptCompressionFilter::None)
			throw std::runtime_error(
			    "A compressed glTF view uses a filter, which the cook never writes: " + name);

		// Each extent held to the total too, so their sum cannot wrap.
		auto &extent = extents[view.bufferIndex];
		extent = std::max(extent, view.byteOffset + view.byteLength);
		if (extent > kMaxDecodedFileBytes)
			throw tooLarge();
		streams.emplace_back(compressed.bufferIndex, compressed.byteOffset, compressed.byteLength);
	}
	if (std::accumulate(extents.begin(), extents.end(), std::size_t{0}) > kMaxDecodedFileBytes)
		throw tooLarge();

	// Each view's stream bytes bound what it decodes to (LeastCompressedBytes) only if no other view
	// names them: four views of one stream once decoded 1 GiB from 256 KiB of it.
	std::ranges::sort(streams);
	for (std::size_t index = 1; index < streams.size(); ++index)
	{
		const auto [buffer, offset, length] = streams[index - 1];
		if (std::get<0>(streams[index]) == buffer && std::get<1>(streams[index]) < offset + length)
			throw std::runtime_error(
			    "Compressed glTF views name the same bytes of a stream, which the cook never writes: " +
			    name);
	}

	std::vector<std::vector<std::byte>> decoded(asset.buffers.size());
	for (std::size_t index = 0; index < asset.buffers.size(); ++index)
		decoded[index].resize(extents[index]);
	for (auto &view : asset.bufferViews)
	{
		if (!view.meshoptCompression)
			continue;
		const fastgltf::CompressedBufferView &compressed = *view.meshoptCompression;
		const auto source = BufferBytes(asset, compressed.bufferIndex);
		const auto *pSource =
		    reinterpret_cast<const unsigned char *>(source.data() + compressed.byteOffset);
		void *pTarget = decoded[view.bufferIndex].data() + view.byteOffset;
		int result = -1;
		switch (compressed.mode)
		{
			case fastgltf::MeshoptCompressionMode::Attributes:
				result = meshopt_decodeVertexBuffer(
				    pTarget, compressed.count, compressed.byteStride, pSource, compressed.byteLength);
				break;
			case fastgltf::MeshoptCompressionMode::Triangles:
				result = meshopt_decodeIndexBuffer(
				    pTarget, compressed.count, compressed.byteStride, pSource, compressed.byteLength);
				break;
			case fastgltf::MeshoptCompressionMode::Indices:
				result = meshopt_decodeIndexSequence(
				    pTarget, compressed.count, compressed.byteStride, pSource, compressed.byteLength);
				break;
		}
		if (result != 0)
			throw std::runtime_error(
			    std::format("A compressed glTF view does not decode ({}): {}", result, name));
		view.meshoptCompression = {};
	}

	for (std::size_t index = 0; index < asset.buffers.size(); ++index)
		if (isFallback(index))
		{
			asset.buffers[index].byteLength = extents[index];
			asset.buffers[index].data = fastgltf::sources::Vector{std::move(decoded[index])};
		}
}


// Every index the file holds names something it has, so a reader can follow one unchecked; only
// what the parser fills is checked, the indices of the extensions in kExtensions with the core's.
void CheckIndices(const fastgltf::Asset &asset, const std::string &name)
{
	const auto check = [&](std::string_view holder, std::size_t at, std::size_t index,
	                       std::size_t count, std::string_view target)
	{
		if (index >= count)
			throw std::runtime_error(
			    std::format(
			        "glTF model's {} {} names {} {}, which the model lacks: {}", holder, at, target,
			        index, name));
	};
	const std::size_t accessors = asset.accessors.size();
	const std::size_t buffers = asset.buffers.size();
	const std::size_t views = asset.bufferViews.size();
	const std::size_t nodes = asset.nodes.size();
	for (std::size_t at = 0; at < views; ++at)
	{
		const fastgltf::BufferView &view = asset.bufferViews[at];
		check("buffer view", at, view.bufferIndex, buffers, "buffer");
		if (view.meshoptCompression)
			check("buffer view", at, view.meshoptCompression->bufferIndex, buffers, "buffer");
	}

	// An accessor with no view is all zeros, or a sparse one's base, and sparse accessors are refused,
	// so none is of use to a reader, which would still make its elements.
	for (std::size_t at = 0; at < accessors; ++at)
	{
		if (!asset.accessors[at].bufferViewIndex)
			throw std::runtime_error(
			    std::format("glTF model's accessor {} has no buffer view: {}", at, name));
		check("accessor", at, *asset.accessors[at].bufferViewIndex, views, "buffer view");
	}
	for (std::size_t at = 0; at < asset.images.size(); ++at)
		if (const auto *pView = std::get_if<fastgltf::sources::BufferView>(&asset.images[at].data))
			check("image", at, pView->bufferViewIndex, views, "buffer view");
	for (std::size_t at = 0; at < asset.textures.size(); ++at)
	{
		const fastgltf::Texture &texture = asset.textures[at];
		if (texture.samplerIndex)
			check("texture", at, *texture.samplerIndex, asset.samplers.size(), "sampler");
		if (texture.imageIndex)
			check("texture", at, *texture.imageIndex, asset.images.size(), "image");
		if (texture.basisuImageIndex)
			check("texture", at, *texture.basisuImageIndex, asset.images.size(), "image");
	}
	for (std::size_t at = 0; at < asset.materials.size(); ++at)
	{
		const fastgltf::Material &material = asset.materials[at];
		const auto slot = [&](const auto &texture)
		{
			if (texture)
				check("material", at, texture->textureIndex, asset.textures.size(), "texture");
		};
		slot(material.pbrData.baseColorTexture);
		slot(material.pbrData.metallicRoughnessTexture);
		slot(material.normalTexture);
		slot(material.occlusionTexture);
		slot(material.emissiveTexture);
	}
	for (std::size_t at = 0; at < asset.meshes.size(); ++at)
		for (const fastgltf::Primitive &primitive : asset.meshes[at].primitives)
		{
			for (const fastgltf::Attribute &attribute : primitive.attributes)
				check("mesh", at, attribute.accessorIndex, accessors, "accessor");
			if (primitive.indicesAccessor)
				check("mesh", at, *primitive.indicesAccessor, accessors, "accessor");
			if (primitive.materialIndex)
				check("mesh", at, *primitive.materialIndex, asset.materials.size(), "material");
		}
	for (std::size_t at = 0; at < nodes; ++at)
	{
		const fastgltf::Node &node = asset.nodes[at];
		if (node.meshIndex)
			check("node", at, *node.meshIndex, asset.meshes.size(), "mesh");
		if (node.skinIndex)
			check("node", at, *node.skinIndex, asset.skins.size(), "skin");
		if (node.cameraIndex)
			check("node", at, *node.cameraIndex, asset.cameras.size(), "camera");
		for (const std::size_t child : node.children)
			check("node", at, child, nodes, "node");
	}
	for (std::size_t at = 0; at < asset.skins.size(); ++at)
	{
		const fastgltf::Skin &skin = asset.skins[at];
		for (const std::size_t joint : skin.joints)
			check("skin", at, joint, nodes, "node");
		if (skin.skeleton)
			check("skin", at, *skin.skeleton, nodes, "node");
		if (skin.inverseBindMatrices)
			check("skin", at, *skin.inverseBindMatrices, accessors, "accessor");
	}
	for (std::size_t at = 0; at < asset.scenes.size(); ++at)
		for (const std::size_t node : asset.scenes[at].nodeIndices)
			check("scene", at, node, nodes, "node");
	if (asset.defaultScene && *asset.defaultScene >= asset.scenes.size())
		throw std::runtime_error(
		    std::format(
		        "glTF model's default scene is scene {}, which the model lacks: {}",
		        *asset.defaultScene, name));
	for (std::size_t at = 0; at < asset.animations.size(); ++at)
	{
		const fastgltf::Animation &animation = asset.animations[at];
		for (const fastgltf::AnimationSampler &sampler : animation.samplers)
		{
			check("animation", at, sampler.inputAccessor, accessors, "accessor");
			check("animation", at, sampler.outputAccessor, accessors, "accessor");
		}
		for (const fastgltf::AnimationChannel &channel : animation.channels)
		{
			check("animation", at, channel.samplerIndex, animation.samplers.size(), "sampler");
			if (channel.nodeIndex)
				check("animation", at, *channel.nodeIndex, nodes, "node");

			// fastgltf keeps a path it does not know as a number of none of its own, which no
			// accessor's use is checked for, and the rig reader read as a scale (the gltf fuzz
			// target's "rota&ion", where fastgltf asserted on a rotation read as a vec3, 2026-11-28).
			using enum fastgltf::AnimationPath;
			if (channel.path != Translation && channel.path != Rotation && channel.path != Scale &&
			    channel.path != Weights)
				throw std::runtime_error(
				    std::format(
				        "glTF model's animation {} has a channel of a path glTF does not define: {}",
				        at, name));
		}
	}
}


// glTF's nodes are trees: a node with two parents, or that is its own ancestor, would send a reader
// walking the hierarchy round for ever.
void CheckHierarchy(const fastgltf::Asset &asset, const std::string &name)
{
	std::vector<bool> parented(asset.nodes.size());
	for (const fastgltf::Node &node : asset.nodes)
		for (const std::size_t child : node.children)
		{
			if (parented[child])
				throw std::runtime_error(
				    std::format("glTF model's node {} has two parents: {}", child, name));
			parented[child] = true;
		}

	// With a parent at most, a node no root reaches is in a cycle, or below one.
	std::vector<std::size_t> reached;
	for (std::size_t node = 0; node < asset.nodes.size(); ++node)
		if (!parented[node])
			reached.push_back(node);
	for (std::size_t next = 0; next < reached.size(); ++next)
		std::ranges::copy(asset.nodes[reached[next]].children, std::back_inserter(reached));
	if (reached.size() != asset.nodes.size())
		throw std::runtime_error("glTF model has a node that is its own ancestor: " + name);
}


// What a file's primitives and clips give its readers to make, held to kMaxReadVertices,
// kMaxReadIndices and kMaxReadKeys by their counts alone, before any bytes are read for them.
void CheckReads(const fastgltf::Asset &asset, const std::string &name)
{
	std::vector<std::size_t> reads(asset.meshes.size());
	for (const fastgltf::Node &node : asset.nodes)
		if (node.meshIndex)
			++reads[*node.meshIndex];
	std::size_t vertices = 0;
	std::size_t indices = 0;
	const auto add = [&](std::size_t &total, std::size_t count, std::size_t times, std::size_t most,
	                     std::string_view holder, std::string_view what)
	{
		if (count > (most - total) / times)
			throw std::runtime_error(
			    std::format(
			        "glTF model's {} give its readers more than {} {} together: {}", holder, most, what,
			        name));
		total += count * times;
	};
	for (std::size_t mesh = 0; mesh < asset.meshes.size(); ++mesh)
	{
		const auto &primitives = asset.meshes[mesh].primitives;
		const std::vector<std::size_t> sharing = FirstSharing(asset.meshes[mesh]);
		std::size_t meshVertices = 0;
		std::size_t meshIndices = 0;
		for (std::size_t at = 0; at < primitives.size(); ++at)
		{
			const fastgltf::Primitive &primitive = primitives[at];
			const auto position = primitive.findAttribute("POSITION");
			const std::size_t count = position == primitive.attributes.end()
			                              ? 0
			                              : asset.accessors[position->accessorIndex].count;
			if (sharing[at] == at)
				add(meshVertices, count, 1, kMaxReadVertices, "primitives", "vertices");
			add(meshIndices,
			    primitive.indicesAccessor ? asset.accessors[*primitive.indicesAccessor].count : count,
			    1, kMaxReadIndices, "primitives", "indices");
		}
		const std::size_t times = std::max(reads[mesh], std::size_t{1});
		add(vertices, meshVertices, times, kMaxReadVertices, "primitives", "vertices");
		add(indices, meshIndices, times, kMaxReadIndices, "primitives", "indices");
	}

	std::size_t keys = 0;
	for (const fastgltf::Animation &animation : asset.animations)
		for (const fastgltf::AnimationChannel &channel : animation.channels)
		{
			const fastgltf::AnimationSampler &sampler = animation.samplers[channel.samplerIndex];
			for (const std::size_t accessor : {sampler.inputAccessor, sampler.outputAccessor})
				add(keys, asset.accessors[accessor].count, 1, kMaxReadKeys, "clips", "keys");
		}
}


// fastgltf's accessor tools and buffer adapter read wherever a view or accessor points, so every view
// is held within its buffer and every accessor within its view, before a reader iterates them.
void CheckBounds(const fastgltf::Asset &asset, const std::string &name)
{
	for (std::size_t index = 0; index < asset.bufferViews.size(); ++index)
	{
		const fastgltf::BufferView &view = asset.bufferViews[index];
		if (!InRange(view.byteOffset, view.byteLength, BufferBytes(asset, view.bufferIndex).size()))
			throw std::runtime_error(
			    std::format("glTF model's buffer view {} lies outside its buffer: {}", index, name));
	}

	for (std::size_t index = 0; index < asset.accessors.size(); ++index)
	{
		const fastgltf::Accessor &accessor = asset.accessors[index];
		if (accessor.sparse)
			throw std::runtime_error(
			    std::format("glTF model's accessor {} is sparse, which is not read: {}", index, name));
		if (fastgltf::validateAccessorBounds(asset, accessor) != fastgltf::Error::None)
			throw std::runtime_error(
			    std::format(
			        "glTF model's accessor {} lies outside its view or buffer: {}", index, name));
	}
}


// An accessor's component type for a use, and whether it is normalised.
struct Component
{
	fastgltf::ComponentType type;
	bool normalized{false};
};

constexpr Component kFloat{fastgltf::ComponentType::Float};
constexpr Component kByte{fastgltf::ComponentType::Byte};
constexpr Component kByteNormalised{fastgltf::ComponentType::Byte, true};
constexpr Component kUnsignedByte{fastgltf::ComponentType::UnsignedByte};
constexpr Component kUnsignedByteNormalised{fastgltf::ComponentType::UnsignedByte, true};
constexpr Component kShort{fastgltf::ComponentType::Short};
constexpr Component kShortNormalised{fastgltf::ComponentType::Short, true};
constexpr Component kUnsignedShort{fastgltf::ComponentType::UnsignedShort};
constexpr Component kUnsignedShortNormalised{fastgltf::ComponentType::UnsignedShort, true};
constexpr Component kUnsignedInt{fastgltf::ComponentType::UnsignedInt};

// The component types glTF 2.0 allows each use, with KHR_mesh_quantization's for attributes:
// POSITION and TEXCOORD_n are quantised, NORMAL and TANGENT directions, COLOR_n and WEIGHTS_n unit
// values, and a clip's rotations and weights animated.
constexpr std::array kFloats{kFloat};
constexpr std::array kQuantised{
    kFloat,
    kByte,
    kByteNormalised,
    kUnsignedByte,
    kUnsignedByteNormalised,
    kShort,
    kShortNormalised,
    kUnsignedShort,
    kUnsignedShortNormalised};
constexpr std::array kDirections{kFloat, kByteNormalised, kShortNormalised};
constexpr std::array kUnitValues{kFloat, kUnsignedByteNormalised, kUnsignedShortNormalised};
constexpr std::array kJoints{kUnsignedByte, kUnsignedShort};
constexpr std::array kIndices{kUnsignedByte, kUnsignedShort, kUnsignedInt};
constexpr std::array kAnimated{
    kFloat, kByteNormalised, kUnsignedByteNormalised, kShortNormalised, kUnsignedShortNormalised};


void CheckUse(
    const fastgltf::Asset &asset, std::size_t index, std::string_view use,
    std::initializer_list<fastgltf::AccessorType> types, std::span<const Component> components,
    const std::string &name)
{
	const fastgltf::Accessor &accessor = asset.accessors[index];
	const bool allowed = std::ranges::find(types, accessor.type) != types.end() &&
	                     std::ranges::any_of(
	                         components,
	                         [&](Component component)
	                         {
		                         return component.type == accessor.componentType &&
		                                component.normalized == accessor.normalized;
	                         });
	if (!allowed)
		throw std::runtime_error(
		    std::format(
		        "glTF model's accessor {} is not a type or component type glTF allows for {}: {}",
		        index, use, name));
}


// An attribute the engine does not know, as an application's own (_NAME), is any type.
void CheckAttribute(
    const fastgltf::Asset &asset, const fastgltf::Attribute &attribute, const std::string &name)
{
	using enum fastgltf::AccessorType;
	const std::string_view semantic = attribute.name;
	const auto check =
	    [&](std::initializer_list<fastgltf::AccessorType> types, std::span<const Component> components)
	{ CheckUse(asset, attribute.accessorIndex, semantic, types, components, name); };
	if (semantic == "POSITION")
		check({Vec3}, kQuantised);
	else if (semantic == "NORMAL")
		check({Vec3}, kDirections);
	else if (semantic == "TANGENT")
		check({Vec4}, kDirections);
	else if (semantic.starts_with("TEXCOORD_"))
		check({Vec2}, kQuantised);
	else if (semantic.starts_with("COLOR_"))
		check({Vec3, Vec4}, kUnitValues);
	else if (semantic.starts_with("JOINTS_"))
		check({Vec4}, kJoints);
	else if (semantic.starts_with("WEIGHTS_"))
		check({Vec4}, kUnitValues);
}


// fastgltf's accessor tools read an element of the type a reader asks for, whatever an accessor holds,
// so each accessor is held to what its use allows. glTF gives a primitive's attributes one count,
// which a reader sizes its vertices by.
void CheckUses(const fastgltf::Asset &asset, const std::string &name)
{
	using enum fastgltf::AccessorType;
	const auto &accessors = asset.accessors;
	for (std::size_t mesh = 0; mesh < asset.meshes.size(); ++mesh)
		for (std::size_t primitive = 0; primitive < asset.meshes[mesh].primitives.size(); ++primitive)
		{
			const fastgltf::Primitive &read = asset.meshes[mesh].primitives[primitive];
			for (const fastgltf::Attribute &attribute : read.attributes)
				CheckAttribute(asset, attribute, name);
			if (read.indicesAccessor)
				CheckUse(asset, *read.indicesAccessor, "indices", {Scalar}, kIndices, name);
			const bool even = std::ranges::all_of(
			    read.attributes,
			    [&](const auto &attribute)
			    {
				    return accessors[attribute.accessorIndex].count ==
				           accessors[read.attributes[0].accessorIndex].count;
			    });
			if (!even)
				throw std::runtime_error(
				    std::format(
				        "glTF mesh {}'s primitive {} has attributes of different counts: {}", mesh,
				        primitive, name));
		}

	for (const fastgltf::Skin &skin : asset.skins)
		if (skin.inverseBindMatrices)
			CheckUse(asset, *skin.inverseBindMatrices, "inverse-bind matrices", {Mat4}, kFloats, name);
	for (const fastgltf::Animation &animation : asset.animations)
	{
		for (const fastgltf::AnimationSampler &sampler : animation.samplers)
			CheckUse(asset, sampler.inputAccessor, "a clip's times", {Scalar}, kFloats, name);
		for (const fastgltf::AnimationChannel &channel : animation.channels)
		{
			const std::size_t output = animation.samplers[channel.samplerIndex].outputAccessor;
			switch (channel.path)
			{
				case fastgltf::AnimationPath::Translation:
				case fastgltf::AnimationPath::Scale:
					CheckUse(asset, output, "a clip's translations or scales", {Vec3}, kFloats, name);
					break;
				case fastgltf::AnimationPath::Rotation:
					CheckUse(asset, output, "a clip's rotations", {Vec4}, kAnimated, name);
					break;
				case fastgltf::AnimationPath::Weights:
					CheckUse(asset, output, "a clip's weights", {Scalar}, kAnimated, name);
					break;
			}
		}
	}
}
} // namespace


bool SameVertices(const fastgltf::Primitive &a, const fastgltf::Primitive &b)
{
	return std::ranges::equal(
	    a.attributes, b.attributes,
	    [](const fastgltf::Attribute &left, const fastgltf::Attribute &right)
	    { return left.name == right.name && left.accessorIndex == right.accessorIndex; });
}


std::vector<std::size_t> FirstSharing(const fastgltf::Mesh &mesh)
{
	using Vertices = std::vector<std::pair<std::string_view, std::size_t>>;
	std::map<Vertices, std::size_t> first;
	std::vector<std::size_t> sharing;
	sharing.reserve(mesh.primitives.size());
	for (std::size_t index = 0; index < mesh.primitives.size(); ++index)
	{
		Vertices vertices;
		for (const fastgltf::Attribute &attribute : mesh.primitives[index].attributes)
			vertices.emplace_back(attribute.name, attribute.accessorIndex);
		sharing.push_back(first.try_emplace(std::move(vertices), index).first->second);
	}
	return sharing;
}


std::optional<std::filesystem::path> FileWithin(
    const std::filesystem::path &directory, const std::filesystem::path &relative)
{
	const bool lexical = !relative.empty() && relative.is_relative() && !relative.has_root_name() &&
	                     std::ranges::none_of(relative, [](const auto &part) { return part == ".."; });
	if (!lexical)
		return std::nullopt;
	std::error_code error;
	const std::filesystem::path root = std::filesystem::canonical(directory, error);
	std::filesystem::path path = error ? std::filesystem::path{}
	                                   : std::filesystem::weakly_canonical(directory / relative, error);
	if (error)
		return std::nullopt;
	const std::filesystem::path within = path.lexically_relative(root);
	if (within.empty() || *within.begin() == "..")
		return std::nullopt;
	return path;
}


std::unique_ptr<GltfFile> GltfFile::Parse(
    std::span<const std::byte> bytes, std::string_view name, const std::filesystem::path &directory)
{
	std::string label{name};
	if (!IsGltf(name))
		throw std::runtime_error("Unsupported model format; expected .gltf or .glb: " + label);
	auto data = fastgltf::GltfDataBuffer::FromBytes(bytes.data(), bytes.size());
	if (data.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to read glTF model: " + label);

	ParsedExtras extras;
	fastgltf::Parser parser{kExtensions};
	parser.setExtrasParseCallback(&ReadExtras);
	parser.setUserPointer(&extras);
	auto parsed = parser.loadGltf(data.get(), {}, fastgltf::Options::DecomposeNodeMatrices);
	if (parsed.error() != fastgltf::Error::None)
		throw std::runtime_error(
		    std::format(
		        "Failed to parse glTF model ({}): {}", fastgltf::getErrorMessage(parsed.error()),
		        label));
	auto &asset = parsed.get();

	if (extras.malformed)
		throw std::runtime_error("glTF model's cook extras are malformed: " + label);

	// Each check before the bytes it allows a reader to take: no buffer file is read, nor any view
	// decoded, for a file whose JSON is refused.
	CheckIndices(asset, label);
	CheckHierarchy(asset, label);
	CheckReads(asset, label);
	ReadBufferFiles(asset, directory, label);
	DecodeCompressedViews(asset, label);
	CheckBounds(asset, label);
	CheckUses(asset, label);

	// A .glb's binary chunk is copied into the asset, so it outlives the data buffer.
	std::unique_ptr<GltfFile> pFile{
	    new GltfFile{std::make_unique<fastgltf::Asset>(std::move(asset)), std::move(label)}};
	pFile->m_cookKey = std::move(extras.cookKey);
	const auto &meshes = pFile->m_pAsset->meshes;
	pFile->m_levels.resize(meshes.size());
	for (auto &[mesh, levels] : extras.levels)
	{
		if (mesh >= meshes.size())
			throw std::runtime_error("glTF model's levels name a mesh it lacks: " + pFile->m_name);
		const std::uint32_t perLevel = levels.front().primitiveCount;
		const bool even = std::ranges::all_of(
		    levels, [&](const MeshLevel &level) { return level.primitiveCount == perLevel; });
		if (!even || std::size_t{perLevel} * levels.size() != meshes[mesh].primitives.size())
			throw std::runtime_error(
			    std::format(
			        "glTF model's levels do not cover mesh {}'s primitives: {}", mesh, pFile->m_name));
		pFile->m_levels[mesh] = std::move(levels);
	}
	for (std::size_t mesh = 0; mesh < meshes.size(); ++mesh)
		if (pFile->m_levels[mesh].empty())
			pFile->m_levels[mesh] = {
			    {0, static_cast<std::uint32_t>(meshes[mesh].primitives.size()), 0.0f}};
	return pFile;
}


GltfFile::GltfFile(std::unique_ptr<fastgltf::Asset> pAsset, std::string name)
    : m_pAsset{std::move(pAsset)}, m_name{std::move(name)}
{
}


GltfFile::~GltfFile() = default;
} // namespace Jettison::Geometry
