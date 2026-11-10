#include <jettison/math/Tangents.h>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <stdexcept>

using namespace Jettison::Math;

namespace
{
const glm::vec3 kUp{0.0f, 0.0f, 1.0f};

void CheckTangent(const glm::vec4 &tangent, const glm::vec4 &expected)
{
	CHECK(tangent.x == doctest::Approx(expected.x));
	CHECK(tangent.y == doctest::Approx(expected.y));
	CHECK(tangent.z == doctest::Approx(expected.z));
	CHECK(tangent.w == expected.w);
}
} // namespace

TEST_CASE("GenerateTangents: a quad whose U follows X has tangent +X, and keeps its four vertices")
{
	const std::array<glm::vec3, 4> positions{
	    glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 0.0f},
	    glm::vec3{0.0f, 1.0f, 0.0f}};
	const std::array<glm::vec3, 4> normals{kUp, kUp, kUp, kUp};
	const std::array<glm::vec2, 4> texCoords{
	    glm::vec2{0.0f, 0.0f}, glm::vec2{1.0f, 0.0f}, glm::vec2{1.0f, 1.0f}, glm::vec2{0.0f, 1.0f}};
	const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};

	const TangentMesh mesh = GenerateTangents(positions, normals, texCoords, indices);
	REQUIRE(mesh.tangents.size() == 4);
	CHECK(mesh.indices == std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
	CHECK(mesh.sourceVertices == std::vector<std::uint32_t>{0, 1, 2, 3});
	for (const glm::vec4 &tangent : mesh.tangents)
		CheckTangent(tangent, glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});
}

// Two quads side by side, the left one's texture mirrored: U runs from 1 at x = -1 to 0 at the
// shared edge and back to 1 at x = 1. The two vertices on the shared edge need a tangent for
// each side, so they are split, and the mirrored side's bitangent sign flips.
TEST_CASE("GenerateTangents: a mirrored UV seam splits the vertices on it")
{
	const std::array<glm::vec3, 6> positions{glm::vec3{-1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 0.0f, 0.0f},
	                                         glm::vec3{0.0f, 1.0f, 0.0f},  glm::vec3{-1.0f, 1.0f, 0.0f},
	                                         glm::vec3{1.0f, 0.0f, 0.0f},  glm::vec3{1.0f, 1.0f, 0.0f}};
	const std::array<glm::vec3, 6> normals{kUp, kUp, kUp, kUp, kUp, kUp};
	const std::array<glm::vec2, 6> texCoords{glm::vec2{1.0f, 0.0f}, glm::vec2{0.0f, 0.0f},
	                                         glm::vec2{0.0f, 1.0f}, glm::vec2{1.0f, 1.0f},
	                                         glm::vec2{1.0f, 0.0f}, glm::vec2{1.0f, 1.0f}};
	// Left quad, then right quad, both wound anticlockwise seen from +Z.
	const std::array<std::uint32_t, 12> indices{0, 1, 2, 0, 2, 3, 1, 4, 5, 1, 5, 2};

	const TangentMesh mesh = GenerateTangents(positions, normals, texCoords, indices);
	REQUIRE(mesh.tangents.size() == 8);
	REQUIRE(mesh.indices.size() == indices.size());
	for (std::size_t corner = 0; corner < indices.size(); ++corner)
	{
		CAPTURE(corner);
		const std::uint32_t output = mesh.indices[corner];
		CHECK(mesh.sourceVertices[output] == indices[corner]);
		if (corner < 6)
			CheckTangent(mesh.tangents[output], glm::vec4{-1.0f, 0.0f, 0.0f, -1.0f});
		else
			CheckTangent(mesh.tangents[output], glm::vec4{1.0f, 0.0f, 0.0f, 1.0f});
	}
}

TEST_CASE("GenerateTangents: refuses input whose parts do not agree")
{
	const std::array<glm::vec3, 3> positions{
	    glm::vec3{0.0f}, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f, 1.0f, 0.0f}};
	const std::array<glm::vec3, 3> normals{kUp, kUp, kUp};
	const std::array<glm::vec2, 3> texCoords{
	    glm::vec2{0.0f}, glm::vec2{1.0f, 0.0f}, glm::vec2{0.0f, 1.0f}};
	const std::array<std::uint32_t, 3> triangle{0, 1, 2};

	CHECK_THROWS_AS(
	    GenerateTangents(positions, std::span{normals}.first(2), texCoords, triangle),
	    std::invalid_argument);
	CHECK_THROWS_AS(
	    GenerateTangents(positions, normals, texCoords, std::span{triangle}.first(2)),
	    std::invalid_argument);
	const std::array<std::uint32_t, 3> outOfRange{0, 1, 3};
	CHECK_THROWS_AS(GenerateTangents(positions, normals, texCoords, outOfRange), std::invalid_argument);
	CHECK(GenerateTangents(positions, normals, texCoords, triangle).tangents.size() == 3);
}
