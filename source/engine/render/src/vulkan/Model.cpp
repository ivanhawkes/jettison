#include <jettison/render/vulkan/Model.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>


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
