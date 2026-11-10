#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace Jettison::Math
{
// An indexed triangle mesh's tangents, as MikkTSpace computes them, which is what glTF specifies
// for a mesh that has a normal map and no tangents. A vertex whose triangles disagree - across a
// UV seam or a mirrored UV island - needs a tangent for each side, so the result is a new
// vertex list: each output vertex names the input vertex whose other attributes it copies.
// Output vertices follow the order the indices first reach them.
struct TangentMesh
{
	// One per input index, into the output vertices.
	std::vector<std::uint32_t> indices;
	// Per output vertex.
	std::vector<std::uint32_t> sourceVertices;
	// Per output vertex: xyz along increasing U, w the sign of the bitangent, cross(normal,
	// tangent) * w, which points along increasing V.
	std::vector<glm::vec4> tangents;
};

// Positions, normals and texture coordinates are per input vertex, and indices form triangles.
// Throws std::invalid_argument when they do not agree: it runs on loaded data.
TangentMesh GenerateTangents(
    std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
    std::span<const glm::vec2> texCoords, std::span<const std::uint32_t> indices);
} // namespace Jettison::Math
