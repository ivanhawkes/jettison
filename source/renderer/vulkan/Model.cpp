#include "Model.h"

// Tiny object loader.
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <algorithm>
#include <cctype>


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
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
		return static_cast<char>(std::tolower(ch));
	});
	if (extension == ".gltf" || extension == ".glb")
	{
		LoadGltfModel(path);
		return;
	}

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.string().c_str()))
	{
		throw std::runtime_error(warn + err);
	}

	std::unordered_map<Vertex, uint32_t> uniqueVertices{};

	for (const auto &shape : shapes)
	{
		for (const auto &index : shape.mesh.indices)
		{
			Vertex vertex{};

			size_t vertex_index = index.vertex_index;
			vertex.pos = {
			    attrib.vertices[3 * vertex_index + 0], attrib.vertices[3 * vertex_index + 1],
			    attrib.vertices[3 * vertex_index + 2]};

			size_t texcoord_index = index.texcoord_index;
			vertex.texCoord = {
			    attrib.texcoords[2 * texcoord_index + 0],
			    1.0f - attrib.texcoords[2 * texcoord_index + 1]};

			if (index.normal_index >= 0)
			{
				const size_t normalIndex = static_cast<size_t>(index.normal_index) * 3;
				vertex.normal = {
				    attrib.normals[normalIndex], attrib.normals[normalIndex + 1],
				    attrib.normals[normalIndex + 2]};
			}

			vertex.color = {1.0f, 1.0f, 1.0f};

			if (uniqueVertices.count(vertex) == 0)
			{
				uniqueVertices[vertex] = static_cast<uint32_t>(m_vertices.size());
				m_vertices.push_back(vertex);
			}

			m_indices.push_back(uniqueVertices[vertex]);
		}
	}

	for (size_t index = 0; index + 2 < m_indices.size(); index += 3)
	{
		Vertex &a = m_vertices[m_indices[index]];
		Vertex &b = m_vertices[m_indices[index + 1]];
		Vertex &c = m_vertices[m_indices[index + 2]];
		const glm::vec3 faceNormal = glm::cross(b.pos - a.pos, c.pos - a.pos);
		if (glm::dot(faceNormal, faceNormal) > 0.0f)
		{
			if (glm::dot(a.normal, a.normal) == 0.0f)
				a.normal += faceNormal;
			if (glm::dot(b.normal, b.normal) == 0.0f)
				b.normal += faceNormal;
			if (glm::dot(c.normal, c.normal) == 0.0f)
				c.normal += faceNormal;
		}
	}
	for (Vertex &vertex : m_vertices)
	{
		if (glm::dot(vertex.normal, vertex.normal) == 0.0f)
			vertex.normal = {0.0f, 0.0f, 1.0f};
		else
			vertex.normal = glm::normalize(vertex.normal);
	}

	CreateVertexBuffer();
	CreateIndexBuffer();
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
}
} // namespace Jettison::Renderer
