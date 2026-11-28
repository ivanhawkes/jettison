#include <jettison/geometry/GltfFile.h>

#include "Extras.h"

#include <fastgltf/core.hpp>
#include <meshoptimizer.h>
#include <simdjson.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>

namespace Jettison::Geometry
{
namespace
{
// What a cooked file uses (Cook.h); a file that does not use them parses the same.
constexpr auto kExtensions = fastgltf::Extensions::EXT_meshopt_compression |
                             fastgltf::Extensions::KHR_mesh_quantization |
                             fastgltf::Extensions::KHR_texture_basisu;

// EXT_meshopt_compression's limits on an attribute view's stride, which meshoptimizer asserts rather
// than checks.
constexpr std::size_t kAttributeStrideAlignment = 4;

constexpr std::size_t kMaxAttributeStride = 256;

constexpr std::size_t kTriangleCorners = 3;


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

		// An error is a float, never negative, and none is less than the finer level's, as the cook
		// writes them.
		const double finer = read.empty() ? 0.0 : double{read.back().error};
		if (level[kPrimitivesName].get(count) != simdjson::SUCCESS ||
		    level[kErrorName].get(errorElement) != simdjson::SUCCESS ||
		    !ReadNumber(errorElement, error) || count == 0 ||
		    count > std::numeric_limits<std::uint32_t>::max() || error < finer ||
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


// A buffer's file, which must lie within directory, so a model's URI cannot read what is outside it.
std::vector<std::byte> ReadBufferFile(
    const std::filesystem::path &directory, const fastgltf::Buffer &buffer, const std::string &name)
{
	const auto &uri = *std::get<fastgltf::sources::URI>(buffer.data).uri;
	const std::optional<std::filesystem::path> path =
	    uri.isLocalPath() ? FileWithin(directory, uri.fspath()) : std::nullopt;
	if (!path)
		throw std::runtime_error(
		    std::format("glTF model's buffer '{}' is outside its directory: {}", uri.string(), name));
	std::ifstream stream{*path, std::ios::binary};
	const std::vector<char> chars{
	    std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
	if (!stream || chars.size() < buffer.byteLength)
		throw std::runtime_error(
		    std::format(
		        "glTF model's buffer '{}' cannot be read, or is shorter than the buffer: {}",
		        uri.string(), name));
	const auto *pBytes = reinterpret_cast<const std::byte *>(chars.data());
	return {pBytes, pBytes + chars.size()};
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


bool InRange(std::size_t offset, std::size_t length, std::size_t size)
{
	return offset <= size && length <= size - offset;
}


// The most one compressed view decodes to, one attribute or one level's indices of one primitive, so
// a file cannot make the reader allocate more for it: 22 million float positions, or 22 million
// triangles' 32-bit indices, about 540 times the largest view the test content cooks to (496,200
// bytes, the large asteroid's 81,920 triangles, measured 2026-11-28).
constexpr std::size_t kMaxDecodedViewBytes = std::size_t{256} * 1024 * 1024;

// The most a fallback buffer decodes to, its furthest view's end, so a view's offset cannot make it
// large when the views are small: four views at their limit, 470 times the large asteroid's buffer
// (2,275,520 bytes).
constexpr std::size_t kMaxDecodedBufferBytes = 4 * kMaxDecodedViewBytes;


// Each compressed view decoded into its fallback buffer, which then holds the bytes the file's
// accessors read, as an uncompressed file's buffer does. A fallback is made only as long as its views
// reach, whatever length it declares.
void DecodeCompressedViews(fastgltf::Asset &asset, const std::string &name)
{
	const auto isFallback = [&](std::size_t index)
	{
		return index < asset.buffers.size() &&
		       std::holds_alternative<fastgltf::sources::Fallback>(asset.buffers[index].data);
	};
	std::vector<std::size_t> extents(asset.buffers.size());
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
		if (compressed.count > kMaxDecodedViewBytes / compressed.byteStride)
			throw std::runtime_error(
			    std::format(
			        "A compressed glTF view decodes to more than {} bytes: {}", kMaxDecodedViewBytes,
			        name));
		if (compressed.count * compressed.byteStride != view.byteLength ||
		    !InRange(view.byteOffset, view.byteLength, asset.buffers[view.bufferIndex].byteLength))
			throw std::runtime_error("A compressed glTF view is out of its buffers' range: " + name);
		auto &extent = extents[view.bufferIndex];
		extent = std::max(extent, view.byteOffset + view.byteLength);
		if (extent > kMaxDecodedBufferBytes)
			throw std::runtime_error(
			    std::format(
			        "A glTF fallback buffer decodes to more than {} bytes: {}", kMaxDecodedBufferBytes,
			        name));
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
		if (!InRange(compressed.byteOffset, compressed.byteLength, source.size()))
			throw std::runtime_error("A compressed glTF view is out of its buffers' range: " + name);
		if (compressed.filter != fastgltf::MeshoptCompressionFilter::None)
			throw std::runtime_error(
			    "A compressed glTF view uses a filter, which the cook never writes: " + name);

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


// fastgltf's accessor tools and buffer adapter read wherever a view or accessor points, so every view
// is held within its buffer, every accessor within its view, and every index naming one to the
// file's, before a reader iterates them. glTF gives a primitive's attributes one count, which a
// reader sizes its vertices by.
void CheckAccessors(const fastgltf::Asset &asset, const std::string &name)
{
	for (std::size_t index = 0; index < asset.bufferViews.size(); ++index)
	{
		const fastgltf::BufferView &view = asset.bufferViews[index];
		if (view.bufferIndex >= asset.buffers.size() ||
		    !InRange(view.byteOffset, view.byteLength, BufferBytes(asset, view.bufferIndex).size()))
			throw std::runtime_error(
			    std::format("glTF model's buffer view {} lies outside its buffer: {}", index, name));
	}

	const auto &accessors = asset.accessors;
	for (std::size_t index = 0; index < accessors.size(); ++index)
	{
		if (accessors[index].sparse)
			throw std::runtime_error(
			    std::format("glTF model's accessor {} is sparse, which is not read: {}", index, name));
		if (fastgltf::validateAccessorBounds(asset, accessors[index]) != fastgltf::Error::None)
			throw std::runtime_error(
			    std::format(
			        "glTF model's accessor {} lies outside its view or buffer: {}", index, name));
	}

	const auto lacks = [&](std::size_t accessor) { return accessor >= accessors.size(); };
	for (std::size_t mesh = 0; mesh < asset.meshes.size(); ++mesh)
		for (std::size_t primitive = 0; primitive < asset.meshes[mesh].primitives.size(); ++primitive)
		{
			const fastgltf::Primitive &read = asset.meshes[mesh].primitives[primitive];
			const bool missing = std::ranges::any_of(
			                         read.attributes, [&](const auto &attribute)
			                         { return lacks(attribute.accessorIndex); }) ||
			                     (read.indicesAccessor && lacks(*read.indicesAccessor));
			if (missing)
				throw std::runtime_error(
				    std::format(
				        "glTF mesh {}'s primitive {} names an accessor the model lacks: {}", mesh,
				        primitive, name));
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

	const bool skinsRead = std::ranges::none_of(
	    asset.skins, [&](const fastgltf::Skin &skin)
	    { return skin.inverseBindMatrices && lacks(*skin.inverseBindMatrices); });
	const bool clipsRead = std::ranges::none_of(
	    asset.animations,
	    [&](const fastgltf::Animation &animation)
	    {
		    return std::ranges::any_of(
		        animation.samplers, [&](const fastgltf::AnimationSampler &sampler)
		        { return lacks(sampler.inputAccessor) || lacks(sampler.outputAccessor); });
	    });
	if (!skinsRead || !clipsRead)
		throw std::runtime_error(
		    "glTF model's skin or clip names an accessor the model lacks: " + name);
}
} // namespace


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

	// Read here rather than by fastgltf's LoadExternalBuffers, which would follow a URI anywhere.
	for (auto &buffer : asset.buffers)
	{
		if (!std::holds_alternative<fastgltf::sources::URI>(buffer.data))
			continue;
		if (directory.empty())
			throw std::runtime_error(
			    "glTF model keeps a buffer in another file, which a resource cannot read: " + label +
			    " (make it a .glb, or embed the buffer)");
		buffer.data = fastgltf::sources::Vector{ReadBufferFile(directory, buffer, label)};
	}
	if (extras.malformed)
		throw std::runtime_error("glTF model's cook extras are malformed: " + label);
	DecodeCompressedViews(asset, label);
	CheckAccessors(asset, label);

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
