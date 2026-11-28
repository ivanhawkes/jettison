#include <jettison/animation/RigLoader.h>
#include <jettison/geometry/GltfFile.h>
#include <jettison/render/Texture.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

#include "Fuzz.h"
#include "vulkan/Model.h"

using namespace Jettison;

namespace
{
constexpr std::size_t kMagicBytes = 4;
} // namespace


extern "C" int LLVMFuzzerInitialize(int * /*argc*/, char *** /*argv*/)
{
	Fuzz::Quiet();
	return 0;
}


// A model file as a resource's bytes, read by GltfFile, then by the two readers of what it accepts:
// the rig's and the renderer's geometry, its base colour images decoded, as the loaders do.
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *pData, std::size_t size)
{
	const bool binary = size >= kMagicBytes && std::memcmp(pData, "glTF", kMagicBytes) == 0;
	std::unique_ptr<Geometry::GltfFile> pFile;
	if (Fuzz::Refuses<std::runtime_error>(
	        [&]
	        {
		        pFile = Geometry::GltfFile::Parse(
		            Fuzz::Bytes(pData, size), binary ? "fuzz.glb" : "fuzz.gltf");
	        }))
		return 0;
	Fuzz::Refuses<std::runtime_error>([&] { Animation::ReadGltfRig(*pFile); });
	std::unique_ptr<Render::Model> pModel;
	if (Fuzz::Refuses<std::runtime_error>([&] { pModel = Render::Model::FromGltf(*pFile); }))
		return 0;
	for (const Render::ModelPrimitive &primitive : pModel->m_primitives)
		if (!primitive.baseColorImageData.empty())
			Fuzz::Refuses<std::runtime_error>(
			    [&] { primitive.DecodeBaseColor(Render::TranscodeTarget::Rgba8, "fuzz"); });
	return 0;
}
