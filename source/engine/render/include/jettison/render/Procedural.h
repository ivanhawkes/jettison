#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

#include <jettison/render/vulkan/Model.h>

namespace Jettison::Render
{
// Geometry made rather than loaded: a mesh's vertices and its triangle list, counter-clockwise
// seen from outside, with normals, texture coordinates and one vertex colour. The planets and
// stations the large-world scene shows are these until phase 5's procedural geometry and LOD.
struct MeshData
{
	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;
};

// The procedural shapes a scene file can name.
enum class Shape
{
	Sphere,
	Box
};

// The shape for a name in a scene file ("sphere", "box"), or false.
bool ParseShape(std::string_view name, Shape &shape);
std::string_view ShapeName(Shape shape);

// A sphere of radius one, `segments` around and segments / 2 from pole to pole. A seam of
// duplicated vertices runs pole to pole, so the texture coordinates wrap.
MeshData UvSphere(int segments, const glm::vec3 &color);
// A cube from -1 to 1, flat-shaded: four vertices to a face.
MeshData Box(const glm::vec3 &color);
MeshData Generate(Shape shape, const glm::vec3 &color);
} // namespace Jettison::Render
