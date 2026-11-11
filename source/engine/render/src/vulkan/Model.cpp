#include <jettison/render/vulkan/Model.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>


namespace Jettison::Render
{
void Model::Destroy()
{
	m_pDeviceContext->DestroyBuffer(m_indexBuffer);
	m_pDeviceContext->DestroyBuffer(m_vertexBuffer);
}


void Model::LoadModel(const std::filesystem::path &path)
{
	auto extension = path.extension().string();
	std::transform(
	    extension.begin(), extension.end(), extension.begin(),
	    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	if (extension != ".gltf" && extension != ".glb")
		throw std::runtime_error("Unsupported model format; expected .gltf or .glb: " + path.string());
	LoadGltfModel(path);
}


void Model::CreateFromMeshes(std::vector<MeshLevel> levels, const std::string &name)
{
	if (levels.empty())
		throw std::runtime_error("A procedural model needs a level of detail: " + name);
	m_vertices.clear();
	m_indices.clear();
	m_primitives.clear();
	m_lods.clear();
	for (MeshLevel &level : levels)
	{
		if (level.vertices.empty() || level.indices.empty())
			throw std::runtime_error("A procedural model needs vertices and indices: " + name);
		// One vertex and index buffer for every level, so a level is a run of indices into it.
		const auto base = static_cast<uint32_t>(m_vertices.size());
		const auto firstIndex = static_cast<uint32_t>(m_indices.size());
		m_vertices.insert(m_vertices.end(), level.vertices.begin(), level.vertices.end());
		for (const uint32_t index : level.indices)
			m_indices.push_back(base + index);
		m_lods.push_back({static_cast<uint32_t>(m_primitives.size()), 1, level.error});
		m_primitives.push_back({firstIndex, static_cast<uint32_t>(level.indices.size()), {}});
	}
	ComputeBounds();
	CreateVertexBuffer();
	CreateIndexBuffer();
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


// The model address names its buffers the same way Pipeline names its descriptor sets, so a
// message about a vertex buffer and a message about a set point at the same model.
void Model::CreateVertexBuffer()
{
	m_vertexBuffer = m_pDeviceContext->CreateDeviceBuffer(
	    m_vertices.data(), static_cast<VkDeviceSize>(sizeof(m_vertices[0])) * m_vertices.size(),
	    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	    "model-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-vertex");
}


void Model::CreateIndexBuffer()
{
	m_indexBuffer = m_pDeviceContext->CreateDeviceBuffer(
	    m_indices.data(), static_cast<VkDeviceSize>(sizeof(m_indices[0])) * m_indices.size(),
	    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	    "model-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-index");
}
} // namespace Jettison::Render
