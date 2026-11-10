#include <jettison/math/Tangents.h>

#include <mikktspace.h>

#include <format>
#include <limits>
#include <new>
#include <stdexcept>

namespace Jettison::Math
{
namespace
{
// What the MikkTSpace callbacks read and write. MikkTSpace calls each face's corners by their
// position in the face, so corner c of face f is index 3f + c.
struct Input
{
	std::span<const glm::vec3> positions;
	std::span<const glm::vec3> normals;
	std::span<const glm::vec2> texCoords;
	std::span<const std::uint32_t> indices;
	std::vector<glm::vec4> cornerTangents;
};

Input &InputOf(const SMikkTSpaceContext *context)
{
	return *static_cast<Input *>(context->m_pUserData);
}

std::size_t Corner(int face, int corner)
{
	return static_cast<std::size_t>(face) * 3 + static_cast<std::size_t>(corner);
}

std::uint32_t VertexOf(const SMikkTSpaceContext *context, int face, int corner)
{
	return InputOf(context).indices[Corner(face, corner)];
}

void Copy(float out[], const glm::vec3 &value)
{
	out[0] = value.x;
	out[1] = value.y;
	out[2] = value.z;
}
} // namespace

TangentMesh GenerateTangents(
    std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
    std::span<const glm::vec2> texCoords, std::span<const std::uint32_t> indices)
{
	if (normals.size() != positions.size() || texCoords.size() != positions.size())
		throw std::invalid_argument{std::format(
		    "GenerateTangents: {} positions, {} normals and {} texture coordinates", positions.size(),
		    normals.size(), texCoords.size())};
	if (indices.size() % 3 != 0)
		throw std::invalid_argument{std::format(
		    "GenerateTangents: {} indices is not a whole number of triangles", indices.size())};
	if (indices.size() / 3 > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		throw std::invalid_argument{"GenerateTangents: more triangles than MikkTSpace can count"};
	for (const std::uint32_t index : indices)
	{
		if (index >= positions.size())
			throw std::invalid_argument{std::format(
			    "GenerateTangents: index {} is past the {} vertices", index, positions.size())};
	}

	Input input{positions, normals, texCoords, indices, std::vector<glm::vec4>(indices.size())};
	SMikkTSpaceInterface callbacks{};
	callbacks.m_getNumFaces = [](const SMikkTSpaceContext *context)
	{ return static_cast<int>(InputOf(context).indices.size() / 3); };
	callbacks.m_getNumVerticesOfFace = [](const SMikkTSpaceContext *, int) { return 3; };
	callbacks.m_getPosition = [](const SMikkTSpaceContext *context, float out[], int face, int corner)
	{ Copy(out, InputOf(context).positions[VertexOf(context, face, corner)]); };
	callbacks.m_getNormal = [](const SMikkTSpaceContext *context, float out[], int face, int corner)
	{ Copy(out, InputOf(context).normals[VertexOf(context, face, corner)]); };
	callbacks.m_getTexCoord = [](const SMikkTSpaceContext *context, float out[], int face, int corner)
	{
		const glm::vec2 &texCoord = InputOf(context).texCoords[VertexOf(context, face, corner)];
		out[0] = texCoord.x;
		out[1] = texCoord.y;
	};
	callbacks.m_setTSpaceBasic =
	    [](const SMikkTSpaceContext *context, const float tangent[], float sign, int face, int corner)
	{
		InputOf(context).cornerTangents[Corner(face, corner)] = {
		    tangent[0], tangent[1], tangent[2], sign};
	};

	SMikkTSpaceContext context{&callbacks, &input};
	if (genTangSpaceDefault(&context) == 0)
		throw std::bad_alloc{}; // Its only failure is an allocation.

	// MikkTSpace gives the corners of one smooth vertex bit-identical tangents, so exact
	// comparison is the weld: an input vertex becomes one output vertex per distinct tangent.
	// Each input vertex heads a chain through the output vertices made from it.
	constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
	std::vector<std::uint32_t> first(positions.size(), kNone);
	std::vector<std::uint32_t> next;
	TangentMesh mesh;
	mesh.indices.reserve(indices.size());
	for (std::size_t corner = 0; corner < indices.size(); ++corner)
	{
		const std::uint32_t source = indices[corner];
		const glm::vec4 &tangent = input.cornerTangents[corner];
		std::uint32_t output = first[source];
		while (output != kNone && mesh.tangents[output] != tangent)
			output = next[output];
		if (output == kNone)
		{
			output = static_cast<std::uint32_t>(mesh.tangents.size());
			mesh.sourceVertices.push_back(source);
			mesh.tangents.push_back(tangent);
			next.push_back(first[source]);
			first[source] = output;
		}
		mesh.indices.push_back(output);
	}
	return mesh;
}
} // namespace Jettison::Math
