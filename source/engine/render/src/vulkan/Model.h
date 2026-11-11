#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/render/Mesh.h>

#include "vulkan/DeviceContext.h"

namespace Jettison::Render
{
// How the mesh programs' vertex input reads a Vertex: one binding, every attribute in it.
VkVertexInputBindingDescription VertexBindingDescription();
std::vector<VkVertexInputAttributeDescription> VertexAttributeDescriptions();

struct ModelPrimitive
{
	uint32_t firstIndex{};
	uint32_t indexCount{};
	// The primitive's own base colour image, encoded as the file had it, until the material cache
	// makes the primitive's material from it.
	std::vector<std::byte> baseColorImageData;
};

// Geometry on the device: one vertex and one index buffer, a primitive per material, and the levels
// of detail as runs of primitives. Made in two steps, so the slow one runs where a resource's decode
// does: the geometry is read or generated with no device (FromGltf on the I/O worker, FromMeshes for
// a procedural shape), then Upload makes the buffers on the main thread and drops the CPU copy. What
// draws with it owns the buffers' release (render/vulkan/ModelCache.h), since a frame in flight may
// still read them when the model goes.
class Model
{
  public:
	Model() = default;
	Model(const Model &) = delete;
	Model &operator=(const Model &) = delete;

	// The geometry in a glTF file's bytes: its one mesh node's triangle primitives, each with the
	// base colour image its material names. A binary .glb, or a .gltf whose buffers and images are
	// embedded, since the bytes are all there is. The node hierarchy, skin and clips are the
	// animation module's to read (Animation::ReadGltfRig). Throws naming the file.
	static std::unique_ptr<Model> FromGltf(std::span<const std::byte> bytes, std::string_view name);

	// A model from geometry made in code: a primitive per level of detail, finest first, untextured.
	// Each level's indices are into its own vertices.
	struct MeshLevel
	{
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		float error{};
	};
	static std::unique_ptr<Model> FromMeshes(std::vector<MeshLevel> levels, std::string_view name);

	// Makes the buffers, named for the validation layer, and frees the CPU copy of the vertices.
	void Upload(DeviceContext &deviceContext, const std::string &name);
	// Hands the buffers over, leaving the model with none: to whoever frees them once no frame in
	// flight reads them.
	void TakeBuffers(Buffer &vertexBuffer, Buffer &indexBuffer);

	// The primitive whose material a primitive of any level draws with.
	uint32_t MaterialPrimitive(uint32_t primitive) const
	{
		for (const ModelLod &lod : m_lods)
			if (primitive >= lod.firstPrimitive && primitive < lod.firstPrimitive + lod.primitiveCount)
				return primitive - lod.firstPrimitive;
		return primitive;
	}

	std::vector<ModelPrimitive> m_primitives{};
	// Finest first; a model file has the one level, of every primitive.
	std::vector<ModelLod> m_lods{};
	ModelBounds m_bounds{};
	Buffer m_vertexBuffer{};
	Buffer m_indexBuffer{};

  private:
	void ReadGltf(std::span<const std::byte> bytes, const std::string &name);
	void ComputeBounds();

	std::vector<Vertex> m_vertices{};
	std::vector<uint32_t> m_indices{};
};
} // namespace Jettison::Render
