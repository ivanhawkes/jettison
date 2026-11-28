#include "Bake.h"

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>

#include <glm/gtc/type_ptr.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Jettison::Bake
{
namespace
{
[[noreturn]] void Refuse(const std::filesystem::path &model, std::string_view problem)
{
	throw std::runtime_error{std::format("Bake: the model '{}' {}", model.string(), problem)};
}


// A mesh node's triangles in the model's axes, three indices each, counter-clockwise from outside.
void ReadTriangles(
    const std::filesystem::path &model, const fastgltf::Asset &asset, const fastgltf::Node &node,
    const glm::mat4 &transform, Physics::ShapeDefinition &part)
{
	for (const fastgltf::Primitive &primitive : asset.meshes[*node.meshIndex].primitives)
	{
		if (primitive.type != fastgltf::PrimitiveType::Triangles)
			Refuse(model, std::format("has a primitive of node '{}' that is not triangles", node.name));
		const auto position = primitive.findAttribute("POSITION");
		if (position == primitive.attributes.end())
			Refuse(model, std::format("has a primitive of node '{}' with no positions", node.name));
		const fastgltf::Accessor &positions = asset.accessors[position->accessorIndex];
		const auto base = static_cast<std::uint32_t>(part.vertices.size());
		fastgltf::iterateAccessor<glm::vec3>(
		    asset, positions,
		    [&](glm::vec3 vertex) { part.vertices.emplace_back(transform * glm::vec4{vertex, 1.0f}); });
		const auto count = static_cast<std::uint32_t>(positions.count);
		if (!primitive.indicesAccessor)
		{
			for (std::uint32_t index = 0; index < count; ++index)
				part.indices.push_back(base + index);
			continue;
		}
		fastgltf::iterateAccessor<std::uint32_t>(
		    asset, asset.accessors[*primitive.indicesAccessor],
		    [&](std::uint32_t index)
		    {
			    if (index >= count)
				    Refuse(
				        model, std::format("has an index past the vertices of node '{}'", node.name));
			    part.indices.push_back(base + index);
		    });
	}

	// A mirroring transform turns the triangles inside out, so their order is turned back.
	if (glm::determinant(glm::mat3{transform}) < 0.0f)
		for (std::size_t first = 0; first + 2 < part.indices.size(); first += 3)
			std::swap(part.indices[first + 1], part.indices[first + 2]);
}
} // namespace


Physics::ShapeDefinition CollisionShape(const std::filesystem::path &model)
{
	auto data = fastgltf::GltfDataBuffer::FromPath(model);
	if (data.error() != fastgltf::Error::None)
		Refuse(model, std::format("does not read: {}", fastgltf::getErrorMessage(data.error())));
	fastgltf::Parser parser;
	auto parsed =
	    parser.loadGltf(data.get(), model.parent_path(), fastgltf::Options::LoadExternalBuffers);
	if (parsed.error() != fastgltf::Error::None)
		Refuse(model, std::format("does not parse: {}", fastgltf::getErrorMessage(parsed.error())));
	const fastgltf::Asset &asset = parsed.get();
	if (asset.scenes.empty())
		Refuse(model, "has no scene");

	const auto authored = [](const fastgltf::Node &node)
	{ return node.meshIndex && std::string_view{node.name}.starts_with(kCollisionPrefix); };
	const bool hasAuthored = std::ranges::any_of(asset.nodes, authored);
	Physics::ShapeDefinition shape;
	shape.kind = Physics::ShapeKind::Compound;
	fastgltf::iterateSceneNodes(
	    asset, asset.defaultScene.value_or(0), fastgltf::math::fmat4x4{},
	    [&](const fastgltf::Node &node, const fastgltf::math::fmat4x4 &matrix)
	    {
		    if (!node.meshIndex || (hasAuthored && !authored(node)))
			    return;
		    Physics::ShapeDefinition part;
		    ReadTriangles(model, asset, node, glm::make_mat4(matrix.data()), part);
		    if (std::string_view{node.name}.starts_with(kCollisionMeshPrefix))
		    {
			    part.kind = Physics::ShapeKind::Mesh;
			    shape.parts.push_back(std::move(part));
			    return;
		    }
		    try
		    {
			    part.points = Physics::ConvexHullPoints(
			        part.vertices, hasAuthored ? kAuthoredConvexHullPoints : kDrawnConvexHullPoints);
		    }
		    catch (const std::invalid_argument &error)
		    {
			    Refuse(
			        model,
			        std::format(
			            "has a node, '{}', that makes no convex hull: {}", node.name, error.what()));
		    }
		    part.kind = Physics::ShapeKind::ConvexHull;
		    part.vertices.clear();
		    part.indices.clear();
		    shape.parts.push_back(std::move(part));
	    });
	if (shape.parts.empty())
		Refuse(model, "has no mesh node in its scene");
	return shape;
}
} // namespace Jettison::Bake
