#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
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
	// A binary .glb, or a .gltf whose buffers are embedded: the bytes are all it is given, so a buffer
	// in a file beside it cannot be read, and is refused. Throws std::runtime_error naming the file for
	// another extension, bytes that are not glTF, such a buffer, a compressed view that does not
	// decode, and levels that do not cover their mesh's primitives.
	static std::unique_ptr<GltfFile> Parse(std::span<const std::byte> bytes, std::string_view name);

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
} // namespace Jettison::Geometry
