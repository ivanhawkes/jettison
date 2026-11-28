#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastgltf
{
class Asset;
}

namespace Jettison::Geometry
{
// A level of detail of one mesh: a run of its primitives and its geometric error, the furthest its
// surface strays from the finest level's, in the mesh's own units. Every level has the primitives
// the first has, in the same order and with the same materials.
struct MeshLevel
{
	std::uint32_t firstPrimitive{};
	std::uint32_t primitiveCount{};
	float error{};
};

// A glTF model file, cooked (Cook.h) or not, parsed once. A cooked file's compressed buffer views are
// decoded here, so fastgltf's accessor tools read them as any other's, and its meshes' levels are
// read from their extras.
class GltfFile
{
  public:
	// A binary .glb, or a .gltf. A buffer in a file of its own is read from directory, at its URI
	// relative to it; with no directory (a resource, whose bytes are all it is given) such a buffer is
	// refused. Images in files of their own are left as URIs. Throws std::runtime_error naming the file
	// for another extension, bytes that are not glTF, a buffer's URI that is absolute, climbs out of
	// the directory (..) or leads out of it through a symbolic link (FileWithin), a buffer file that
	// cannot be read or is shorter than the buffer, a compressed view whose stride or count breaks
	// EXT_meshopt_compression, that lies outside its buffers, that decodes to more than 256 MiB or
	// into a fallback buffer past 1 GiB, or that does not decode, a view outside its buffer, an
	// accessor that is sparse or lies outside its view or buffer, a primitive, skin or clip naming an
	// accessor the file lacks, a primitive whose attributes differ in count, and levels that are empty,
	// have an error that is negative, past a float's range or less than the finer level's, or do not
	// cover their mesh's primitives. A file it returns can be read by fastgltf's accessor tools, at
	// those indices, without reading outside its buffers.
	static std::unique_ptr<GltfFile> Parse(
	    std::span<const std::byte> bytes, std::string_view name,
	    const std::filesystem::path &directory = {});

	~GltfFile();

	GltfFile(const GltfFile &) = delete;

	GltfFile &operator=(const GltfFile &) = delete;

	// fastgltf's asset, for a reader that links fastgltf itself; it owns its buffers.
	const fastgltf::Asset &Asset() const
	{
		return *m_pAsset;
	}

	// The virtual path it was read from, for errors.
	const std::string &Name() const
	{
		return m_name;
	}

	// A mesh's levels, finest first. A file that was not cooked has one level a mesh, of all its
	// primitives and error 0.
	std::span<const MeshLevel> Levels(std::size_t mesh) const
	{
		return m_levels.at(mesh);
	}

	// What the cook wrote it from (CookModel's cookKey); empty for a file that was not cooked.
	const std::string &CookKey() const
	{
		return m_cookKey;
	}

  private:
	GltfFile(std::unique_ptr<fastgltf::Asset> pAsset, std::string name);

	std::unique_ptr<fastgltf::Asset> m_pAsset;
	std::string m_name;
	std::vector<std::vector<MeshLevel>> m_levels;
	std::string m_cookKey;
};

// The file at relative within directory, by its weakly canonical path: relative is relative, with no
// "..", and leads to a path within directory with symbolic links followed, since a link inside it can
// point out of it. The rule GltfFile::Parse holds a .gltf's buffers to, for a reader of a model's
// other files (the cook, of its images). Empty for any other path. A file that is not there is not
// refused, so reading it fails as reading any missing file does.
std::optional<std::filesystem::path> FileWithin(
    const std::filesystem::path &directory, const std::filesystem::path &relative);
} // namespace Jettison::Geometry
