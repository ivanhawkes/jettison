#include "Model.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <functional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Jettison::Renderer
{
namespace
{
glm::vec3 ToGlm(const fastgltf::math::nvec3 &value)
{
	return {value[0], value[1], value[2]};
}

glm::quat ToGlm(const fastgltf::math::nquat &value)
{
	return {value.w(), value.x(), value.y(), value.z()};
}

ModelNode ReadNode(const fastgltf::Node &source)
{
	ModelNode result;
	if (const auto *trs = std::get_if<fastgltf::TRS>(&source.transform))
	{
		result.translation = ToGlm(trs->translation);
		result.rotation = ToGlm(trs->rotation);
		result.scale = ToGlm(trs->scale);
		return result;
	}

	const auto &matrix = std::get<fastgltf::math::nmat4x4>(source.transform);
	fastgltf::math::nvec3 scale;
	fastgltf::math::nquat rotation;
	fastgltf::math::nvec3 translation;
	fastgltf::math::decomposeTransformMatrix(matrix, scale, rotation, translation);
	result.translation = ToGlm(translation);
	result.rotation = ToGlm(rotation);
	result.scale = ToGlm(scale);
	return result;
}

glm::mat4 LocalTransform(const ModelNode &node)
{
	glm::mat4 matrix = glm::translate(glm::mat4{1.0f}, node.translation);
	matrix *= glm::mat4_cast(node.rotation);
	return glm::scale(matrix, node.scale);
}

float SampleInterval(
    const std::vector<float> &times, float time, std::size_t &first, std::size_t &second)
{
	if (times.empty())
		return 0.0f;
	const auto upper = std::upper_bound(times.begin(), times.end(), time);
	if (upper == times.begin())
	{
		first = second = 0;
		return 0.0f;
	}
	if (upper == times.end())
	{
		first = second = times.size() - 1;
		return 0.0f;
	}
	second = static_cast<std::size_t>(upper - times.begin());
	first = second - 1;
	const float duration = times[second] - times[first];
	return duration > 0.0f ? (time - times[first]) / duration : 0.0f;
}

glm::vec4 SampleTrack(const ModelAnimationTrack &track, float time)
{
	if (track.times.empty())
		return glm::vec4{0.0f};

	std::size_t first = 0;
	std::size_t second = 0;
	const float factor = SampleInterval(track.times, time, first, second);
	if (track.interpolation == ModelAnimationInterpolation::CubicSpline)
	{
		const glm::vec4 start = track.values[first * 3 + 1];
		if (first == second)
			return start;
		const glm::vec4 outTangent = track.values[first * 3 + 2];
		const glm::vec4 end = track.values[second * 3 + 1];
		const glm::vec4 inTangent = track.values[second * 3];
		const float delta = track.times[second] - track.times[first];
		const float t2 = factor * factor;
		const float t3 = t2 * factor;
		return (2.0f * t3 - 3.0f * t2 + 1.0f) * start + (t3 - 2.0f * t2 + factor) * delta * outTangent +
		       (-2.0f * t3 + 3.0f * t2) * end + (t3 - t2) * delta * inTangent;
	}

	const glm::vec4 start = track.values[first];
	if (first == second || track.interpolation == ModelAnimationInterpolation::Step)
		return start;
	const glm::vec4 end = track.values[second];
	if (track.path == ModelAnimationPath::Rotation)
	{
		const glm::quat startRotation{start.w, start.x, start.y, start.z};
		const glm::quat endRotation{end.w, end.x, end.y, end.z};
		const glm::quat rotation = glm::normalize(glm::slerp(startRotation, endRotation, factor));
		return {rotation.x, rotation.y, rotation.z, rotation.w};
	}
	return glm::mix(start, end, factor);
}

std::vector<std::byte> ReadBaseColorImageData(
    const fastgltf::Asset &asset, const fastgltf::Primitive &primitive,
    const std::filesystem::path &path)
{
	if (!primitive.materialIndex)
		return {};
	if (*primitive.materialIndex >= asset.materials.size())
		throw std::runtime_error("glTF material index is out of range: " + path.string());
	const auto &baseColorTexture = asset.materials[*primitive.materialIndex].pbrData.baseColorTexture;
	if (!baseColorTexture)
		return {};
	if (baseColorTexture->textureIndex >= asset.textures.size())
		throw std::runtime_error("glTF base-color texture index is out of range: " + path.string());
	const auto &imageIndex = asset.textures[baseColorTexture->textureIndex].imageIndex;
	if (!imageIndex || *imageIndex >= asset.images.size())
		throw std::runtime_error("glTF base-color image is missing: " + path.string());

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
		throw std::runtime_error("Unsupported glTF base-color image source: " + path.string());
	return {imageData.begin(), imageData.end()};
}
} // namespace

void Model::LoadGltfModel(const std::filesystem::path &path)
{
	auto data = fastgltf::GltfDataBuffer::FromPath(path);
	if (data.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to read glTF model: " + path.string());

	fastgltf::Parser parser;
	const auto options = fastgltf::Options::LoadExternalBuffers |
	                     fastgltf::Options::LoadExternalImages |
	                     fastgltf::Options::DecomposeNodeMatrices;
	auto parsed = parser.loadGltf(data.get(), path.parent_path(), options);
	if (parsed.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to parse glTF model: " + path.string());
	const fastgltf::Asset &asset = parsed.get();
	if (asset.nodes.empty())
		throw std::runtime_error("glTF model has no nodes: " + path.string());

	m_nodes.resize(asset.nodes.size());
	for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
		m_nodes[nodeIndex] = ReadNode(asset.nodes[nodeIndex]);
	for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
	{
		for (const std::size_t child : asset.nodes[nodeIndex].children)
		{
			if (child >= m_nodes.size() || m_nodes[child].parent >= 0)
				throw std::runtime_error("glTF node hierarchy has an invalid or repeated parent");
			m_nodes[child].parent = static_cast<int>(nodeIndex);
		}
	}

	std::optional<std::size_t> meshNode;
	for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
	{
		if (!asset.nodes[nodeIndex].meshIndex)
			continue;
		if (meshNode)
			throw std::runtime_error(
			    "This renderer currently expects one mesh node per glTF asset: " + path.string());
		meshNode = nodeIndex;
	}
	if (!meshNode)
		throw std::runtime_error("glTF model has no mesh node: " + path.string());
	m_meshNodeIndex = *meshNode;
	const auto meshIndex = *asset.nodes[m_meshNodeIndex].meshIndex;
	if (meshIndex >= asset.meshes.size())
		throw std::runtime_error("glTF mesh index is out of range: " + path.string());

	if (asset.nodes[m_meshNodeIndex].skinIndex)
	{
		const std::size_t skinIndex = *asset.nodes[m_meshNodeIndex].skinIndex;
		if (skinIndex >= asset.skins.size())
			throw std::runtime_error("glTF skin index is out of range: " + path.string());
		const auto &skin = asset.skins[skinIndex];
		m_joints.reserve(skin.joints.size());
		std::vector<glm::mat4> inverseBindMatrices(skin.joints.size(), glm::mat4{1.0f});
		if (skin.inverseBindMatrices)
		{
			const auto &accessor = asset.accessors[*skin.inverseBindMatrices];
			if (accessor.count != skin.joints.size())
				throw std::runtime_error("glTF inverse-bind matrix count does not match joint count");
			fastgltf::iterateAccessorWithIndex<glm::mat4>(
			    asset, accessor,
			    [&](glm::mat4 matrix, std::size_t index) { inverseBindMatrices[index] = matrix; });
		}
		for (std::size_t index = 0; index < skin.joints.size(); ++index)
		{
			if (skin.joints[index] >= m_nodes.size())
				throw std::runtime_error("glTF skin joint node index is out of range");
			m_joints.push_back({skin.joints[index], inverseBindMatrices[index]});
		}
	}

	for (const auto &primitive : asset.meshes[meshIndex].primitives)
	{
		if (primitive.type != fastgltf::PrimitiveType::Triangles)
			throw std::runtime_error("Only triangle glTF primitives are supported: " + path.string());
		ModelPrimitive modelPrimitive;
		modelPrimitive.firstIndex = static_cast<uint32_t>(m_indices.size());
		modelPrimitive.baseColorImageData = ReadBaseColorImageData(asset, primitive, path);
		const auto positionAttribute = primitive.findAttribute("POSITION");
		if (positionAttribute == primitive.attributes.end())
			throw std::runtime_error("glTF triangle primitive has no POSITION attribute");

		const auto &positionAccessor = asset.accessors[positionAttribute->accessorIndex];
		const std::size_t vertexOffset = m_vertices.size();
		std::vector<glm::vec3> positions(positionAccessor.count);
		fastgltf::iterateAccessorWithIndex<glm::vec3>(
		    asset, positionAccessor,
		    [&](glm::vec3 value, std::size_t index) { positions[index] = value; });
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
		if (!m_joints.empty() && jointsAttribute == primitive.attributes.end())
			throw std::runtime_error("Skinned glTF primitive is missing JOINTS_0 and WEIGHTS_0");
		if ((jointsAttribute == primitive.attributes.end()) !=
		    (weightsAttribute == primitive.attributes.end()))
			throw std::runtime_error("glTF primitive must provide both JOINTS_0 and WEIGHTS_0");
		if ((jointsAttribute1 == primitive.attributes.end()) !=
		    (weightsAttribute1 == primitive.attributes.end()))
			throw std::runtime_error("glTF primitive must provide both JOINTS_1 and WEIGHTS_1");
		if (jointsAttribute1 != primitive.attributes.end() &&
		    jointsAttribute == primitive.attributes.end())
			throw std::runtime_error("glTF JOINTS_1 requires JOINTS_0");
		if (jointsAttribute != primitive.attributes.end())
		{
			if (m_joints.empty())
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

		m_vertices.reserve(m_vertices.size() + positions.size());
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
			if (!m_joints.empty())
			{
				// Indexing a glm vector narrows to its length_type (int), and the joint
				// indices in the vertex are 32-bit unsigned, so the count joins them there
				// rather than narrowing six times per vertex.
				const auto jointCount = static_cast<glm::uvec4::value_type>(m_joints.size());
				for (int influence = 0; influence < 4; ++influence)
				{
					if ((vertex.weights[influence] > 0.0f && vertex.joints[influence] >= jointCount) ||
					    (vertex.weights1[influence] > 0.0f && vertex.joints1[influence] >= jointCount))
						throw std::runtime_error(
						    "glTF vertex joint index exceeds the skin joint count");
				}
			}
			m_vertices.push_back(vertex);
		}

		if (primitive.indicesAccessor)
		{
			const auto &indicesAccessor = asset.accessors[*primitive.indicesAccessor];
			fastgltf::iterateAccessor<std::uint32_t>(
			    asset, indicesAccessor,
			    [&](std::uint32_t index)
			    {
				    if (index >= positions.size())
					    throw std::runtime_error("glTF mesh index is out of range");
				    m_indices.push_back(static_cast<std::uint32_t>(vertexOffset + index));
			    });
		}
		else
		{
			for (std::size_t index = 0; index < positions.size(); ++index)
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

	for (const auto &animation : asset.animations)
	{
		ModelAnimationClip clip;
		clip.name = animation.name.empty() ? "Animation " + std::to_string(m_animations.size())
		                                   : animation.name;
		for (const auto &channel : animation.channels)
		{
			if (!channel.nodeIndex || channel.path == fastgltf::AnimationPath::Weights)
				continue;
			if (*channel.nodeIndex >= m_nodes.size() ||
			    channel.samplerIndex >= animation.samplers.size())
				throw std::runtime_error("glTF animation references an invalid node or sampler");
			const auto &sampler = animation.samplers[channel.samplerIndex];
			ModelAnimationTrack track;
			track.nodeIndex = *channel.nodeIndex;
			track.path =
			    channel.path == fastgltf::AnimationPath::Translation ? ModelAnimationPath::Translation
			    : channel.path == fastgltf::AnimationPath::Rotation  ? ModelAnimationPath::Rotation
			                                                         : ModelAnimationPath::Scale;
			track.interpolation =
			    sampler.interpolation == fastgltf::AnimationInterpolation::Step
			        ? ModelAnimationInterpolation::Step
			    : sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline
			        ? ModelAnimationInterpolation::CubicSpline
			        : ModelAnimationInterpolation::Linear;
			const auto &inputAccessor = asset.accessors[sampler.inputAccessor];
			fastgltf::iterateAccessor<float>(
			    asset, inputAccessor, [&](float value) { track.times.push_back(value); });
			const auto &outputAccessor = asset.accessors[sampler.outputAccessor];
			track.values.reserve(outputAccessor.count);
			if (track.path == ModelAnimationPath::Rotation)
			{
				fastgltf::iterateAccessor<glm::vec4>(
				    asset, outputAccessor, [&](glm::vec4 value) { track.values.push_back(value); });
			}
			else
			{
				fastgltf::iterateAccessor<glm::vec3>(
				    asset, outputAccessor,
				    [&](glm::vec3 value) { track.values.emplace_back(value, 0.0f); });
			}
			if (!track.times.empty())
				clip.duration = std::max(clip.duration, track.times.back());
			clip.tracks.push_back(std::move(track));
		}
		m_animations.push_back(std::move(clip));
	}

	CreateVertexBuffer();
	CreateIndexBuffer();
}

bool Model::HasAnimation(std::string_view name) const
{
	return std::any_of(
	    m_animations.begin(), m_animations.end(),
	    [&](const ModelAnimationClip &clip) { return clip.name == name; });
}

float Model::AnimationDuration(std::string_view name) const
{
	const auto clip = std::find_if(
	    m_animations.begin(), m_animations.end(),
	    [&](const ModelAnimationClip &value) { return value.name == name; });
	return clip == m_animations.end() ? 0.0f : clip->duration;
}

void Model::EvaluateSkin(
    std::string_view animationName, float time, std::vector<glm::mat4> &jointMatrices,
    glm::mat4 &meshNodeTransform) const
{
	if (m_nodes.empty() || m_meshNodeIndex >= m_nodes.size())
		throw std::runtime_error("Cannot evaluate a skin without glTF node data");
	std::vector<ModelNode> pose = m_nodes;
	if (!animationName.empty())
	{
		const auto clip = std::find_if(
		    m_animations.begin(), m_animations.end(),
		    [&](const ModelAnimationClip &value) { return value.name == animationName; });
		if (clip == m_animations.end())
			throw std::runtime_error("Unknown glTF animation clip: " + std::string(animationName));
		for (const ModelAnimationTrack &track : clip->tracks)
		{
			const glm::vec4 value = SampleTrack(track, time);
			ModelNode &node = pose[track.nodeIndex];
			if (track.path == ModelAnimationPath::Translation)
				node.translation = glm::vec3(value);
			else if (track.path == ModelAnimationPath::Scale)
				node.scale = glm::vec3(value);
			else
				node.rotation = glm::normalize(glm::quat(value.w, value.x, value.y, value.z));
		}
	}

	std::vector<glm::mat4> worldTransforms(m_nodes.size(), glm::mat4{1.0f});
	std::vector<std::uint8_t> states(m_nodes.size(), 0);
	std::function<const glm::mat4 &(std::size_t)> computeWorld =
	    [&](std::size_t index) -> const glm::mat4 &
	{
		if (states[index] == 2)
			return worldTransforms[index];
		if (states[index] == 1)
			throw std::runtime_error("glTF node hierarchy contains a cycle");
		states[index] = 1;
		const glm::mat4 local = LocalTransform(pose[index]);
		worldTransforms[index] =
		    pose[index].parent >= 0 ? computeWorld(static_cast<std::size_t>(pose[index].parent)) * local
		                            : local;
		states[index] = 2;
		return worldTransforms[index];
	};
	for (std::size_t index = 0; index < pose.size(); ++index)
		computeWorld(index);

	meshNodeTransform = worldTransforms[m_meshNodeIndex];
	const glm::mat4 inverseMeshTransform = glm::inverse(meshNodeTransform);
	jointMatrices.resize(m_joints.size());
	for (std::size_t index = 0; index < m_joints.size(); ++index)
		jointMatrices[index] = inverseMeshTransform * worldTransforms[m_joints[index].nodeIndex] *
		                       m_joints[index].inverseBindMatrix;
}
} // namespace Jettison::Renderer
