#include "vulkan/Model.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace Jettison::Render
{
VkVertexInputBindingDescription VertexBindingDescription()
{
	VkVertexInputBindingDescription bindingDescription{};
	bindingDescription.binding = 0;
	bindingDescription.stride = sizeof(Vertex);
	bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription> VertexAttributeDescriptions()
{
	return {
	    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
	    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
	    {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord)},
	    {3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
	    {4, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, joints)},
	    {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights)},
	    {6, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, joints1)},
	    {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, weights1)}};
}

std::unique_ptr<Model> Model::FromGltf(std::span<const std::byte> bytes, std::string_view name)
{
	auto pModel = std::make_unique<Model>();
	pModel->ReadGltf(bytes, std::string{name});
	return pModel;
}

std::unique_ptr<Model> Model::FromMeshes(std::vector<MeshLevel> levels, std::string_view name)
{
	if (levels.empty())
		throw std::runtime_error("A procedural model needs a level of detail: " + std::string{name});
	auto pModel = std::make_unique<Model>();
	Model &model = *pModel;
	for (MeshLevel &level : levels)
	{
		if (level.vertices.empty() || level.indices.empty())
			throw std::runtime_error(
			    "A procedural model needs vertices and indices: " + std::string{name});
		// One vertex and index buffer for every level, so a level is a run of indices into it.
		const auto base = static_cast<uint32_t>(model.m_vertices.size());
		const auto firstIndex = static_cast<uint32_t>(model.m_indices.size());
		model.m_vertices.insert(model.m_vertices.end(), level.vertices.begin(), level.vertices.end());
		for (const uint32_t index : level.indices)
			model.m_indices.push_back(base + index);
		model.m_lods.push_back({static_cast<uint32_t>(model.m_primitives.size()), 1, level.error});
		model.m_primitives.push_back({firstIndex, static_cast<uint32_t>(level.indices.size()), {}});
	}
	model.ComputeBounds();
	return pModel;
}

void Model::Upload(DeviceContext &deviceContext, const std::string &name)
{
	m_vertexBuffer = deviceContext.CreateDeviceBuffer(
	    m_vertices.data(), static_cast<VkDeviceSize>(sizeof(m_vertices[0])) * m_vertices.size(),
	    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, name + "-vertex");
	m_indexBuffer = deviceContext.CreateDeviceBuffer(
	    m_indices.data(), static_cast<VkDeviceSize>(sizeof(m_indices[0])) * m_indices.size(),
	    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, name + "-index");
	// The device has its copy; the primitives keep their index counts, and the bounds are made.
	std::vector<Vertex>{}.swap(m_vertices);
	std::vector<uint32_t>{}.swap(m_indices);
}

void Model::TakeBuffers(Buffer &vertexBuffer, Buffer &indexBuffer)
{
	vertexBuffer = std::exchange(m_vertexBuffer, {});
	indexBuffer = std::exchange(m_indexBuffer, {});
}

void Model::ComputeBounds()
{
	if (m_vertices.empty())
	{
		m_bounds = {};
		return;
	}
	glm::vec3 minimum = m_vertices.front().pos;
	glm::vec3 maximum = minimum;
	for (const Vertex &vertex : m_vertices)
	{
		minimum = glm::min(minimum, vertex.pos);
		maximum = glm::max(maximum, vertex.pos);
	}
	// Around the box's centre, out to the furthest vertex: tighter than the box's own corners.
	m_bounds.center = (minimum + maximum) * 0.5f;
	float radiusSquared = 0.0f;
	for (const Vertex &vertex : m_vertices)
	{
		const glm::vec3 offset = vertex.pos - m_bounds.center;
		radiusSquared = std::max(radiusSquared, glm::dot(offset, offset));
	}
	m_bounds.radius = std::sqrt(radiusSquared);
}
} // namespace Jettison::Render
