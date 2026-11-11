#include <jettison/animation/RigLoader.h>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <jettison/resource/ResourceManager.h>

namespace Jettison::Animation
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

RigNode ReadNode(const fastgltf::Node &source)
{
	RigNode result;
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

Clip ReadClip(const fastgltf::Asset &asset, const fastgltf::Animation &animation, std::size_t index)
{
	Clip clip;
	clip.name = animation.name.empty() ? "Animation " + std::to_string(index) : animation.name;
	for (const auto &channel : animation.channels)
	{
		if (!channel.nodeIndex || channel.path == fastgltf::AnimationPath::Weights)
			continue;
		if (*channel.nodeIndex >= asset.nodes.size() ||
		    channel.samplerIndex >= animation.samplers.size())
			throw std::runtime_error("glTF animation references an invalid node or sampler");
		const auto &sampler = animation.samplers[channel.samplerIndex];
		Track track;
		track.nodeIndex = *channel.nodeIndex;
		track.path = channel.path == fastgltf::AnimationPath::Translation ? TrackPath::Translation
		             : channel.path == fastgltf::AnimationPath::Rotation  ? TrackPath::Rotation
		                                                                  : TrackPath::Scale;
		track.interpolation = sampler.interpolation == fastgltf::AnimationInterpolation::Step
		                          ? Interpolation::Step
		                      : sampler.interpolation == fastgltf::AnimationInterpolation::CubicSpline
		                          ? Interpolation::CubicSpline
		                          : Interpolation::Linear;
		fastgltf::iterateAccessor<float>(
		    asset, asset.accessors[sampler.inputAccessor],
		    [&](float value) { track.times.push_back(value); });
		const auto &outputAccessor = asset.accessors[sampler.outputAccessor];
		track.values.reserve(outputAccessor.count);
		if (track.path == TrackPath::Rotation)
			fastgltf::iterateAccessor<glm::vec4>(
			    asset, outputAccessor, [&](glm::vec4 value) { track.values.push_back(value); });
		else
			fastgltf::iterateAccessor<glm::vec3>(
			    asset, outputAccessor,
			    [&](glm::vec3 value) { track.values.emplace_back(value, 0.0f); });
		if (!track.times.empty())
			clip.duration = std::max(clip.duration, track.times.back());
		clip.tracks.push_back(std::move(track));
	}
	return clip;
}
} // namespace

Rig ReadGltfRig(std::span<const std::byte> bytes, std::string_view name)
{
	const std::string label{name};
	auto data = fastgltf::GltfDataBuffer::FromBytes(bytes.data(), bytes.size());
	if (data.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to read glTF model: " + label);
	fastgltf::Parser parser;
	auto parsed = parser.loadGltf(data.get(), {}, fastgltf::Options::DecomposeNodeMatrices);
	if (parsed.error() != fastgltf::Error::None)
		throw std::runtime_error("Failed to parse glTF model: " + label);
	const fastgltf::Asset &asset = parsed.get();
	for (const auto &buffer : asset.buffers)
		if (std::holds_alternative<fastgltf::sources::URI>(buffer.data))
			throw std::runtime_error(
			    "glTF model keeps a buffer in another file, which a resource cannot read: " + label +
			    " (make it a .glb, or embed the buffer)");
	if (asset.nodes.empty())
		throw std::runtime_error("glTF model has no nodes: " + label);

	Rig rig;
	rig.nodes.resize(asset.nodes.size());
	for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
		rig.nodes[nodeIndex] = ReadNode(asset.nodes[nodeIndex]);
	for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
	{
		for (const std::size_t child : asset.nodes[nodeIndex].children)
		{
			if (child >= rig.nodes.size() || rig.nodes[child].parent >= 0)
				throw std::runtime_error("glTF node hierarchy has an invalid or repeated parent");
			rig.nodes[child].parent = static_cast<int>(nodeIndex);
		}
	}

	std::optional<std::size_t> meshNode;
	for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex)
	{
		if (!asset.nodes[nodeIndex].meshIndex)
			continue;
		if (meshNode)
			throw std::runtime_error(
			    "This renderer currently expects one mesh node per glTF asset: " + label);
		meshNode = nodeIndex;
	}
	if (!meshNode)
		throw std::runtime_error("glTF model has no mesh node: " + label);
	rig.meshNodeIndex = *meshNode;

	if (asset.nodes[rig.meshNodeIndex].skinIndex)
	{
		const std::size_t skinIndex = *asset.nodes[rig.meshNodeIndex].skinIndex;
		if (skinIndex >= asset.skins.size())
			throw std::runtime_error("glTF skin index is out of range: " + label);
		const auto &skin = asset.skins[skinIndex];
		rig.joints.reserve(skin.joints.size());
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
			if (skin.joints[index] >= rig.nodes.size())
				throw std::runtime_error("glTF skin joint node index is out of range");
			rig.joints.push_back({skin.joints[index], inverseBindMatrices[index]});
		}
	}

	for (std::size_t index = 0; index < asset.animations.size(); ++index)
		rig.clips.push_back(ReadClip(asset, asset.animations[index], index));
	return rig;
}

void RegisterRigLoader(Resource::ResourceManager &resources)
{
	resources.RegisterLoader(
	    Resource::Loader<Rig>{
	        [](std::span<const std::byte> bytes, std::string_view path)
	        { return std::make_unique<Rig>(ReadGltfRig(bytes, path)); },
	        {},
	        {}});
}
} // namespace Jettison::Animation
