#include <jettison/geometry/GltfFile.h>

#include "Extras.h"

#include <fastgltf/core.hpp>
#include <meshoptimizer.h>
#include <simdjson.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <limits>
#include <stdexcept>
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
	if (ours[kLevelsName].get(levels) != simdjson::SUCCESS)
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
		if (level[kPrimitivesName].get(count) != simdjson::SUCCESS ||
		    level[kErrorName].get(errorElement) != simdjson::SUCCESS ||
		    !ReadNumber(errorElement, error) || count == 0 ||
		    count > std::numeric_limits<std::uint32_t>::max())
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


// Each compressed view decoded into its fallback buffer, which then holds the bytes the file's
// accessors read, as an uncompressed file's buffer does.
void DecodeCompressedViews(fastgltf::Asset &asset, const std::string &name)
{
	std::vector<std::vector<std::byte>> decoded(asset.buffers.size());
	for (std::size_t index = 0; index < asset.buffers.size(); ++index)
		if (std::holds_alternative<fastgltf::sources::Fallback>(asset.buffers[index].data))
			decoded[index].resize(asset.buffers[index].byteLength);

	for (auto &view : asset.bufferViews)
	{
		if (!view.meshoptCompression)
			continue;
		const fastgltf::CompressedBufferView &compressed = *view.meshoptCompression;
		const auto source = BufferBytes(asset, compressed.bufferIndex);
		if (view.bufferIndex >= decoded.size() || decoded[view.bufferIndex].empty())
			throw std::runtime_error(
			    "A compressed glTF view decodes into a buffer that is not its fallback: " + name);
		auto &target = decoded[view.bufferIndex];
		if (compressed.byteOffset + compressed.byteLength > source.size() ||
		    view.byteOffset + view.byteLength > target.size() ||
		    compressed.count * compressed.byteStride != view.byteLength)
			throw std::runtime_error("A compressed glTF view is out of its buffers' range: " + name);
		if (compressed.filter != fastgltf::MeshoptCompressionFilter::None)
			throw std::runtime_error(
			    "A compressed glTF view uses a filter, which the cook never writes: " + name);

		const auto *pSource =
		    reinterpret_cast<const unsigned char *>(source.data() + compressed.byteOffset);
		void *pTarget = target.data() + view.byteOffset;
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
		if (std::holds_alternative<fastgltf::sources::Fallback>(asset.buffers[index].data))
			asset.buffers[index].data = fastgltf::sources::Vector{std::move(decoded[index])};
}
} // namespace


std::unique_ptr<GltfFile> GltfFile::Parse(std::span<const std::byte> bytes, std::string_view name)
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
	for (const auto &buffer : asset.buffers)
		if (std::holds_alternative<fastgltf::sources::URI>(buffer.data))
			throw std::runtime_error(
			    "glTF model keeps a buffer in another file, which a resource cannot read: " + label +
			    " (make it a .glb, or embed the buffer)");
	if (extras.malformed)
		throw std::runtime_error("glTF model's cook extras are malformed: " + label);
	DecodeCompressedViews(asset, label);

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
