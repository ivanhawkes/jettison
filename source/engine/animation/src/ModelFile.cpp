#include <jettison/animation/ModelFile.h>

#include <fastgltf/core.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>
#include <variant>

namespace Jettison::Animation
{
namespace
{
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
} // namespace


std::unique_ptr<ModelFile> ModelFile::Parse(std::span<const std::byte> bytes, std::string_view name)
{
	std::string label{name};
	if (!IsGltf(name))
		throw std::runtime_error("Unsupported model format; expected .gltf or .glb: " + label);
	auto data = fastgltf::GltfDataBuffer::FromBytes(bytes.data(), bytes.size());
	if (data.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to read glTF model: " + label);
	fastgltf::Parser parser;
	auto parsed = parser.loadGltf(data.get(), {}, fastgltf::Options::DecomposeNodeMatrices);
	if (parsed.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to parse glTF model: " + label);
	for (const auto &buffer : parsed.get().buffers)
		if (std::holds_alternative<fastgltf::sources::URI>(buffer.data))
			throw std::runtime_error(
			    "glTF model keeps a buffer in another file, which a resource cannot read: " + label +
			    " (make it a .glb, or embed the buffer)");

	// A .glb's binary chunk is copied into the asset, so it outlives the data buffer.
	return std::unique_ptr<ModelFile>{
	    new ModelFile{std::make_unique<fastgltf::Asset>(std::move(parsed.get())), std::move(label)}};
}


ModelFile::ModelFile(std::unique_ptr<fastgltf::Asset> pAsset, std::string name)
    : m_pAsset{std::move(pAsset)}, m_name{std::move(name)}
{
}


ModelFile::~ModelFile() = default;
} // namespace Jettison::Animation
