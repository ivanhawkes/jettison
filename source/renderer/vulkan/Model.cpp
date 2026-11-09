#include "Model.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>


namespace Jettison::Renderer
{
void Model::Destroy()
{
	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), m_indexBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), m_indexBufferMemory, nullptr);

	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), m_vertexBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), m_vertexBufferMemory, nullptr);
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


void Model::CreateVertexBuffer()
{
	VkDeviceSize bufferSize = static_cast<VkDeviceSize>(sizeof(m_vertices[0])) * m_vertices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	m_pDeviceContext->CreateBuffer(
	    bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
	    stagingBufferMemory);

	void *data;
	vkMapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, m_vertices.data(), static_cast<size_t>(bufferSize));
	vkUnmapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory);

	m_pDeviceContext->CreateBuffer(
	    bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexBufferMemory);

	m_pDeviceContext->CopyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), stagingBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, nullptr);
	// The model address names this buffer the same way Pipeline names its descriptor sets, so a
	// message about a vertex buffer and a message about a set point at the same model.
	m_pDeviceContext->SetDebugName(
	    m_vertexBuffer, "model-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-vertex");
}


void Model::CreateIndexBuffer()
{
	VkDeviceSize bufferSize = static_cast<VkDeviceSize>(sizeof(m_indices[0])) * m_indices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	m_pDeviceContext->CreateBuffer(
	    bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
	    stagingBufferMemory);

	void *data;
	vkMapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, m_indices.data(), static_cast<size_t>(bufferSize));
	vkUnmapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory);

	m_pDeviceContext->CreateBuffer(
	    bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
	    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexBufferMemory);

	m_pDeviceContext->CopyBuffer(stagingBuffer, m_indexBuffer, bufferSize);

	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), stagingBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, nullptr);
	m_pDeviceContext->SetDebugName(
	    m_indexBuffer, "model-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + "-index");
}
} // namespace Jettison::Renderer
