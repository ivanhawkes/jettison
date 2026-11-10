#pragma once

#include <vulkan/vulkan.h>

// GL Math. GLM's configuration macros are set on the glm target in source/external, not here:
// they have to precede the first GLM include in every translation unit, which a header set
// them in can only do for the files that include it first.
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

// STD.
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/animation/Rig.h>
#include <jettison/render/vulkan/DeviceContext.h>


namespace Jettison::Render
{
struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
	glm::vec3 normal;
	glm::uvec4 joints{0};
	glm::vec4 weights{0.0f};
	glm::uvec4 joints1{0};
	glm::vec4 weights1{0.0f};

	static VkVertexInputBindingDescription getBindingDescription()
	{
		VkVertexInputBindingDescription bindingDescription{};
		bindingDescription.binding = 0;
		bindingDescription.stride = sizeof(Vertex);
		bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		return bindingDescription;
	}


	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions()
	{
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{
		    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
		    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
		    {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)},
		    {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
		    {4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, joints)},
		    {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights)},
		    {6, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, joints1)},
		    {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights1)}};

		return attributeDescriptions;
	}


	bool operator==(const Vertex &other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord &&
		       normal == other.normal && joints == other.joints && weights == other.weights &&
		       joints1 == other.joints1 && weights1 == other.weights1;
	}
};

struct ModelPrimitive
{
	uint32_t firstIndex{};
	uint32_t indexCount{};
	std::vector<std::byte> baseColorImageData;
};


class Model
{
  public:
	Model(std::shared_ptr<DeviceContext> pDeviceContext) : m_pDeviceContext{pDeviceContext}
	{
	}

	// Disable copying.
	Model() = default;
	Model(const Model &) = delete;
	Model &operator=(const Model &) = delete;

	void Destroy();

	void LoadModel(const std::filesystem::path &path);
	// A model from geometry made in code: one primitive, untextured, with no rig. `name` names
	// its buffers for the validation layer.
	void CreateFromMesh(
	    std::vector<Vertex> vertices, std::vector<uint32_t> indices, const std::string &name);

	// The node hierarchy, skin and clips read from the file, for the clip player in
	// engine/animation. The model owns them because the glTF loader reads them alongside the
	// vertices; nothing in them needs the device.
	const Animation::Rig &GetRig() const
	{
		return m_rig;
	}

	std::vector<uint32_t> m_indices{};
	std::vector<ModelPrimitive> m_primitives{};
	Buffer m_vertexBuffer{};
	Buffer m_indexBuffer{};

  private:
	void LoadGltfModel(const std::filesystem::path &path);
	void CreateVertexBuffer();

	void CreateIndexBuffer();

	std::shared_ptr<DeviceContext> m_pDeviceContext;

	std::vector<Vertex> m_vertices{};
	Animation::Rig m_rig{};
};
} // namespace Jettison::Render


namespace std
{
template <> struct hash<Jettison::Render::Vertex>
{
	size_t operator()(const Jettison::Render::Vertex &vertex) const
	{
		return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
		       (hash<glm::vec2>()(vertex.texCoord) << 1) ^ (hash<glm::vec3>()(vertex.normal) << 2) ^
		       (hash<glm::uvec4>()(vertex.joints) << 3) ^ (hash<glm::vec4>()(vertex.weights) << 4) ^
		       (hash<glm::uvec4>()(vertex.joints1) << 5) ^ (hash<glm::vec4>()(vertex.weights1) << 6);
	}
};
} // namespace std
