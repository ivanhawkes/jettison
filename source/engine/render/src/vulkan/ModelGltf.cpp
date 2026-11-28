#include "vulkan/Model.h"

#include <jettison/core/Log.h>
#include <jettison/geometry/GltfFile.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace Jettison::Render
{
namespace
{
constexpr Core::Category kRender{"render"};


// The primitive's base colour image, and whether it is KTX 2: a cooked file's textures name theirs
// through KHR_texture_basisu alone, with no fallback.
void ReadBaseColorImageData(
    const fastgltf::Asset &asset, const fastgltf::Primitive &primitive, const std::string &path,
    ModelPrimitive &target)
{
	if (!primitive.materialIndex)
		return;
	if (*primitive.materialIndex >= asset.materials.size())
		throw std::runtime_error("glTF material index is out of range: " + path);
	const auto &baseColorTexture = asset.materials[*primitive.materialIndex].pbrData.baseColorTexture;
	if (!baseColorTexture)
		return;
	if (baseColorTexture->textureIndex >= asset.textures.size())
		throw std::runtime_error("glTF base-color texture index is out of range: " + path);
	const auto &texture = asset.textures[baseColorTexture->textureIndex];
	target.baseColorKtx2 = texture.basisuImageIndex.has_value();
	const auto &imageIndex = target.baseColorKtx2 ? texture.basisuImageIndex : texture.imageIndex;
	if (!imageIndex || *imageIndex >= asset.images.size())
		throw std::runtime_error("glTF base-color image is missing: " + path);

	const auto &image = asset.images[*imageIndex];
	const auto imageData = std::visit(
	    [&](const auto &source) -> std::span<const std::byte>
	    {
		    using Source = std::decay_t<decltype(source)>;
		    if constexpr (std::is_same_v<Source, fastgltf::sources::BufferView>)
			    return fastgltf::DefaultBufferDataAdapter{}(asset, source.bufferViewIndex);
		    else if constexpr (std::is_same_v<Source, fastgltf::sources::Array>)
			    return {source.bytes.data(), source.bytes.size_bytes()};
		    else if constexpr (std::is_same_v<Source, fastgltf::sources::Vector>)
			    return {source.bytes.data(), source.bytes.size()};
		    else if constexpr (std::is_same_v<Source, fastgltf::sources::ByteView>)
			    return source.bytes;
		    else
			    return {};
	    },
	    image.data);
	if (imageData.empty())
		throw std::runtime_error("Unsupported glTF base-color image source: " + path);
	target.baseColorImageData.assign(imageData.begin(), imageData.end());
}


// A primitive's vertices, its normals left zero where the file has none.
std::vector<Vertex> ReadVertices(
    const fastgltf::Asset &asset, const fastgltf::Primitive &primitive, std::size_t skinJoints)
{
	const auto positionAttribute = primitive.findAttribute("POSITION");
	if (positionAttribute == primitive.attributes.end())
		throw std::runtime_error("glTF triangle primitive has no POSITION attribute");

	const auto &positionAccessor = asset.accessors[positionAttribute->accessorIndex];
	std::vector<glm::vec3> positions(positionAccessor.count);
	fastgltf::iterateAccessorWithIndex<glm::vec3>(
	    asset, positionAccessor, [&](glm::vec3 value, std::size_t index) { positions[index] = value; });
	std::vector<glm::vec3> normals(positionAccessor.count, glm::vec3{0.0f});
	if (const auto attribute = primitive.findAttribute("NORMAL");
	    attribute != primitive.attributes.end())
	{
		fastgltf::iterateAccessorWithIndex<glm::vec3>(
		    asset, asset.accessors[attribute->accessorIndex],
		    [&](glm::vec3 value, std::size_t index) { normals[index] = value; });
	}
	std::vector<glm::vec2> texCoords(positionAccessor.count, glm::vec2{0.0f});
	if (const auto attribute = primitive.findAttribute("TEXCOORD_0");
	    attribute != primitive.attributes.end())
	{
		fastgltf::iterateAccessorWithIndex<glm::vec2>(
		    asset, asset.accessors[attribute->accessorIndex],
		    [&](glm::vec2 value, std::size_t index) { texCoords[index] = value; });
	}
	std::vector<glm::u16vec4> joints(positionAccessor.count, glm::u16vec4{0});
	std::vector<glm::vec4> weights(positionAccessor.count, glm::vec4{0.0f});
	std::vector<glm::u16vec4> joints1(positionAccessor.count, glm::u16vec4{0});
	std::vector<glm::vec4> weights1(positionAccessor.count, glm::vec4{0.0f});
	const auto jointsAttribute = primitive.findAttribute("JOINTS_0");
	const auto weightsAttribute = primitive.findAttribute("WEIGHTS_0");
	const auto jointsAttribute1 = primitive.findAttribute("JOINTS_1");
	const auto weightsAttribute1 = primitive.findAttribute("WEIGHTS_1");
	if (skinJoints != 0 && jointsAttribute == primitive.attributes.end())
		throw std::runtime_error("Skinned glTF primitive is missing JOINTS_0 and WEIGHTS_0");
	if ((jointsAttribute == primitive.attributes.end()) !=
	    (weightsAttribute == primitive.attributes.end()))
		throw std::runtime_error("glTF primitive must provide both JOINTS_0 and WEIGHTS_0");
	if ((jointsAttribute1 == primitive.attributes.end()) !=
	    (weightsAttribute1 == primitive.attributes.end()))
		throw std::runtime_error("glTF primitive must provide both JOINTS_1 and WEIGHTS_1");
	if (jointsAttribute1 != primitive.attributes.end() && jointsAttribute == primitive.attributes.end())
		throw std::runtime_error("glTF JOINTS_1 requires JOINTS_0");
	if (jointsAttribute != primitive.attributes.end())
	{
		if (skinJoints == 0)
			throw std::runtime_error("glTF primitive has skin attributes but its node has no skin");
		fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
		    asset, asset.accessors[jointsAttribute->accessorIndex],
		    [&](glm::u16vec4 value, std::size_t index) { joints[index] = value; });
		fastgltf::iterateAccessorWithIndex<glm::vec4>(
		    asset, asset.accessors[weightsAttribute->accessorIndex],
		    [&](glm::vec4 value, std::size_t index) { weights[index] = value; });
		if (jointsAttribute1 != primitive.attributes.end())
		{
			fastgltf::iterateAccessorWithIndex<glm::u16vec4>(
			    asset, asset.accessors[jointsAttribute1->accessorIndex],
			    [&](glm::u16vec4 value, std::size_t index) { joints1[index] = value; });
			fastgltf::iterateAccessorWithIndex<glm::vec4>(
			    asset, asset.accessors[weightsAttribute1->accessorIndex],
			    [&](glm::vec4 value, std::size_t index) { weights1[index] = value; });
		}
	}

	std::vector<Vertex> vertices;
	vertices.reserve(positions.size());
	for (std::size_t index = 0; index < positions.size(); ++index)
	{
		Vertex vertex{};
		vertex.pos = positions[index];
		vertex.normal = normals[index];
		vertex.texCoord = texCoords[index];
		vertex.color = glm::vec3{1.0f};
		vertex.joints = glm::uvec4{joints[index]};
		vertex.weights = weights[index];
		vertex.joints1 = glm::uvec4{joints1[index]};
		vertex.weights1 = weights1[index];
		const float weightSum = vertex.weights.x + vertex.weights.y + vertex.weights.z +
		                        vertex.weights.w + vertex.weights1.x + vertex.weights1.y +
		                        vertex.weights1.z + vertex.weights1.w;
		if (weightSum > 0.0f)
		{
			vertex.weights /= weightSum;
			vertex.weights1 /= weightSum;
		}
		if (skinJoints != 0)
		{
			// Indexing a glm vector narrows to its length_type (int), and the joint
			// indices in the vertex are 32-bit unsigned, so the count joins them there
			// rather than narrowing six times per vertex.
			const auto jointCount = static_cast<glm::uvec4::value_type>(skinJoints);
			for (int influence = 0; influence < 4; ++influence)
			{
				if ((vertex.weights[influence] > 0.0f && vertex.joints[influence] >= jointCount) ||
				    (vertex.weights1[influence] > 0.0f && vertex.joints1[influence] >= jointCount))
					throw std::runtime_error("glTF vertex joint index exceeds the skin joint count");

				// The shader reads every influence's joint matrix, weighed or not, so one of no weight
				// is pointed at the first joint rather than past the skin's.
				if (!(vertex.weights[influence] > 0.0f))
					vertex.joints[influence] = 0;
				if (!(vertex.weights1[influence] > 0.0f))
					vertex.joints1[influence] = 0;
			}
		}
		vertices.push_back(vertex);
	}
	return vertices;
}


// Whether two primitives read the same accessors for every attribute, as a cooked file's levels of
// one source primitive do.
bool SameVertices(const fastgltf::Primitive &a, const fastgltf::Primitive &b)
{
	return std::ranges::equal(
	    a.attributes, b.attributes,
	    [](const fastgltf::Attribute &left, const fastgltf::Attribute &right)
	    { return left.name == right.name && left.accessorIndex == right.accessorIndex; });
}
} // namespace


void Model::ReadGltf(const Geometry::GltfFile &file)
{
	const fastgltf::Asset &asset = file.Asset();
	const std::string &path = file.Name();
	std::optional<std::size_t> meshNode;
	for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
	{
		if (!asset.nodes[nodeIndex].meshIndex)
			continue;
		if (meshNode)
			throw std::runtime_error(
			    "This renderer currently expects one mesh node per glTF asset: " + path);
		meshNode = nodeIndex;
	}
	if (!meshNode)
		throw std::runtime_error("glTF model has no mesh node: " + path);
	const auto meshIndex = *asset.nodes[*meshNode].meshIndex;
	if (meshIndex >= asset.meshes.size())
		throw std::runtime_error("glTF mesh index is out of range: " + path);

	// The skin's joints, which the vertices' joint indices are into; the rig that poses them is the
	// animation module's.
	std::size_t skinJoints = 0;
	if (const auto skinIndex = asset.nodes[*meshNode].skinIndex)
	{
		if (*skinIndex >= asset.skins.size())
			throw std::runtime_error("glTF skin index is out of range: " + path);
		skinJoints = asset.skins[*skinIndex].joints.size();
	}

	struct ReadPrimitive
	{
		const fastgltf::Primitive *pPrimitive{};
		std::size_t vertexOffset{};
		std::size_t vertexCount{};
	};

	std::vector<ReadPrimitive> read;

	// Every level has the finest's materials, which the material cache makes from its primitives.
	const auto levels = file.Levels(meshIndex);
	for (const auto &primitive : asset.meshes[meshIndex].primitives)
	{
		if (primitive.type != fastgltf::PrimitiveType::Triangles)
			throw std::runtime_error("Only triangle glTF primitives are supported: " + path);
		ModelPrimitive modelPrimitive;
		modelPrimitive.firstIndex = static_cast<uint32_t>(m_indices.size());
		if (m_primitives.size() < levels.front().primitiveCount)
			ReadBaseColorImageData(asset, primitive, path, modelPrimitive);

		// A cooked file's levels share their source primitive's vertices, which are read once.
		auto shared = std::ranges::find_if(
		    read,
		    [&](const ReadPrimitive &earlier) { return SameVertices(*earlier.pPrimitive, primitive); });
		if (shared == read.end())
		{
			const std::vector<Vertex> vertices = ReadVertices(asset, primitive, skinJoints);
			read.push_back({&primitive, m_vertices.size(), vertices.size()});
			m_vertices.insert(m_vertices.end(), vertices.begin(), vertices.end());
			shared = std::prev(read.end());
		}
		const std::size_t vertexOffset = shared->vertexOffset;
		const std::size_t vertexCount = shared->vertexCount;

		if (primitive.indicesAccessor)
		{
			const auto &indicesAccessor = asset.accessors[*primitive.indicesAccessor];
			fastgltf::iterateAccessor<std::uint32_t>(
			    asset, indicesAccessor,
			    [&](std::uint32_t index)
			    {
				    if (index >= vertexCount)
					    throw std::runtime_error("glTF mesh index is out of range");
				    m_indices.push_back(static_cast<std::uint32_t>(vertexOffset + index));
			    });
		}
		else
		{
			for (std::size_t index = 0; index < vertexCount; ++index)
				m_indices.push_back(static_cast<std::uint32_t>(vertexOffset + index));
		}
		modelPrimitive.indexCount = static_cast<uint32_t>(m_indices.size()) - modelPrimitive.firstIndex;
		m_primitives.push_back(std::move(modelPrimitive));
	}

	for (std::size_t index = 0; index + 2 < m_indices.size(); index += 3)
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

	for (const Geometry::MeshLevel &level : levels)
		m_lods.push_back({level.firstPrimitive, level.primitiveCount, level.error});
	if (file.CookKey().empty())
		Core::Log(
		    Core::Severity::Warning, kRender,
		    "model {} was not cooked, so it draws as one level of detail; cook it with jettison_cook",
		    path);
	ComputeBounds();
}
} // namespace Jettison::Render
