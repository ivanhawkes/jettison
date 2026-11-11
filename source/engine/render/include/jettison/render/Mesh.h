#pragma once

// GL Math. GLM's configuration macros are set on the glm target in source/external, not here:
// they have to precede the first GLM include in every translation unit, which a header set
// them in can only do for the files that include it first.
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>

namespace Jettison::Render
{
// One vertex of a mesh, as the mesh programs read it (assets/shaders/include/mesh_vertex.glsl); the
// Vulkan backend describes its attributes to the pipeline.
struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 texCoord;
	glm::vec3 normal;
	glm::uvec4 joints{0};
	glm::vec4 weights{0.0f};
	glm::uvec4 joints1{0};
	glm::vec4 weights1{0.0f};

	bool operator==(const Vertex &other) const
	{
		return pos == other.pos && color == other.color && texCoord == other.texCoord &&
		       normal == other.normal && joints == other.joints && weights == other.weights &&
		       joints1 == other.joints1 && weights1 == other.weights1;
	}
};

// A level of detail: a run of a model's primitives, and its geometric error, the furthest its
// surface strays from the model's finest, in the model's own units. Every level has the primitives
// the first has, in the same order, so a primitive's material is the first level's primitive at the
// same place.
struct ModelLod
{
	std::uint32_t firstPrimitive{};
	std::uint32_t primitiveCount{};
	float error{};
};

// A sphere around every vertex of the model, in its own space: what culling tests. A skinned model's
// pose moves its vertices out of it, so a skinned draw is never culled.
struct ModelBounds
{
	glm::vec3 center{0.0f};
	float radius{0.0f};
};
} // namespace Jettison::Render

namespace std
{
template <> struct hash<Jettison::Render::Vertex>
{
	size_t operator()(const Jettison::Render::Vertex &vertex) const
	{
		return ((hash<glm::vec3>()(vertex.pos) ^ (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
		       (hash<glm::vec2>()(vertex.texCoord) << 1) ^ (hash<glm::vec3>()(vertex.normal) << 2) ^
		       (hash<glm::uvec4>()(vertex.joints) << 3) ^ (hash<glm::vec4>()(vertex.weights) << 4) ^
		       (hash<glm::uvec4>()(vertex.joints1) << 5) ^ (hash<glm::vec4>()(vertex.weights1) << 6);
	}
};
} // namespace std
