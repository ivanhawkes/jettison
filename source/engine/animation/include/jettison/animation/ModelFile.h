#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace fastgltf
{
class Asset;
}

namespace Jettison::Animation
{
// A glTF model file, read and parsed once for both its rig (RigLoader.h) and the renderer's geometry,
// each made from it as a Resource::DerivedLoader. It lives here because the rig's reader does and
// render depends on this module, never the other way. Its resource is not kept (Loader::keep): it is
// dropped once the rig and the geometry asked for with it are made, so the file is not held in memory
// beside them.
class ModelFile
{
  public:
	// A binary .glb, or a .gltf whose buffers are embedded: the bytes are all a resource's decode is
	// given, so a buffer in a file beside it cannot be read, and is refused. Throws naming the file
	// for another extension, for bytes that are not glTF and for such a buffer.
	static std::unique_ptr<ModelFile> Parse(std::span<const std::byte> bytes, std::string_view name);

	~ModelFile();

	ModelFile(const ModelFile &) = delete;

	ModelFile &operator=(const ModelFile &) = delete;

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

  private:
	ModelFile(std::unique_ptr<fastgltf::Asset> pAsset, std::string name);

	std::unique_ptr<fastgltf::Asset> m_pAsset;
	std::string m_name;
};
} // namespace Jettison::Animation
