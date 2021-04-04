#include "Model.h"

// Tiny object loader.
#define TINYOBJLOADER_IMPLEMENTATION
#include <../tiny_obj_loader/include/tiny_obj_loader.h>


namespace Jettison::Renderer
{
const std::string kModelPath = "assets/models/viking_room.wobj";


void Model::Destroy()
{
	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), m_indexBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), m_indexBufferMemory, nullptr);

	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), m_vertexBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), m_vertexBufferMemory, nullptr);
}


void Model::LoadModel()
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn;
	std::string err;

	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, kModelPath.c_str()))
	{
		throw std::runtime_error(warn + err);
	}

	std::unordered_map<Vertex, uint32_t> uniqueVertices {};

	for (const auto& shape : shapes)
	{
		for (const auto& index : shape.mesh.indices)
		{
			Vertex vertex {};

			size_t vertex_index = index.vertex_index;
			vertex.pos = {
				attrib.vertices[3 * vertex_index + 0],
				attrib.vertices[3 * vertex_index + 1],
				attrib.vertices[3 * vertex_index + 2]
			};

			size_t texcoord_index = index.texcoord_index;
			vertex.texCoord = {
				attrib.texcoords[2 * texcoord_index + 0],
				1.0f - attrib.texcoords[2 * texcoord_index + 1]
			};

			vertex.color = {1.0f, 1.0f, 1.0f};

			if (uniqueVertices.count(vertex) == 0)
			{
				uniqueVertices[vertex] = static_cast<uint32_t>(m_vertices.size());
				m_vertices.push_back(vertex);
			}

			m_indices.push_back(uniqueVertices[vertex]);
		}
	}

	CreateVertexBuffer();
	CreateIndexBuffer();
}


void Model::CreateVertexBuffer()
{
	VkDeviceSize bufferSize = static_cast<VkDeviceSize>(sizeof(m_vertices[0])) * m_vertices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	m_pDeviceContext->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, m_vertices.data(), static_cast<size_t>(bufferSize));
	vkUnmapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory);

	m_pDeviceContext->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertexBuffer, m_vertexBufferMemory);

	m_pDeviceContext->CopyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), stagingBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, nullptr);
}


void Model::CreateIndexBuffer()
{
	VkDeviceSize bufferSize = static_cast<VkDeviceSize>(sizeof(m_indices[0])) * m_indices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	m_pDeviceContext->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

	void* data;
	vkMapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, m_indices.data(), static_cast<size_t>(bufferSize));
	vkUnmapMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory);

	m_pDeviceContext->CreateBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_indexBuffer, m_indexBufferMemory);

	m_pDeviceContext->CopyBuffer(stagingBuffer, m_indexBuffer, bufferSize);

	vkDestroyBuffer(m_pDeviceContext->GetLogicalDevice(), stagingBuffer, nullptr);
	vkFreeMemory(m_pDeviceContext->GetLogicalDevice(), stagingBufferMemory, nullptr);
}
}
