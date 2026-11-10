#include <jettison/render/Procedural.h>

#include <doctest/doctest.h>

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace Jettison::Render;

namespace
{
// The mesh with vertices that share a position merged, as indices into one position each. A seam
// or a face's own vertices are separate in the vertex buffer but the same point on the surface.
std::vector<std::uint32_t> Weld(const MeshData &mesh)
{
	std::map<std::tuple<long, long, long>, std::uint32_t> byPosition;
	std::vector<std::uint32_t> welded(mesh.vertices.size());
	for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
	{
		const glm::vec3 &position = mesh.vertices[index].pos;
		// A grid of a millionth: far finer than any shape's spacing, far coarser than its rounding.
		const auto key = std::make_tuple(
		    std::lround(position.x * 1e6f), std::lround(position.y * 1e6f),
		    std::lround(position.z * 1e6f));
		welded[index] =
		    byPosition.emplace(key, static_cast<std::uint32_t>(byPosition.size())).first->second;
	}
	std::vector<std::uint32_t> indices;
	indices.reserve(mesh.indices.size());
	for (const std::uint32_t index : mesh.indices)
		indices.push_back(welded[index]);
	return indices;
}

// Closed and consistently wound once welded: every directed edge is used once, by one triangle,
// and its reverse by one other. A hole leaves an edge without a twin; a flipped triangle repeats a
// directed edge.
bool IsClosedAndOriented(const MeshData &mesh)
{
	const std::vector<std::uint32_t> indices = Weld(mesh);
	std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
	for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3)
		for (std::size_t corner = 0; corner < 3; ++corner)
		{
			const std::uint32_t from = indices[triangle + corner];
			const std::uint32_t to = indices[triangle + (corner + 1) % 3];
			if (from == to)
				return false;
			++edges[{from, to}];
		}
	for (const auto &[edge, count] : edges)
	{
		if (count != 1)
			return false;
		const auto twin = edges.find({edge.second, edge.first});
		if (twin == edges.end() || twin->second != 1)
			return false;
	}
	return true;
}

// Positive for triangles wound anticlockwise seen from outside.
double SignedVolume(const MeshData &mesh)
{
	double volume = 0.0;
	for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
	{
		const glm::dvec3 a{mesh.vertices[mesh.indices[triangle]].pos};
		const glm::dvec3 b{mesh.vertices[mesh.indices[triangle + 1]].pos};
		const glm::dvec3 c{mesh.vertices[mesh.indices[triangle + 2]].pos};
		volume += glm::dot(a, glm::cross(b, c)) / 6.0;
	}
	return volume;
}

bool NormalsAreUnit(const MeshData &mesh)
{
	for (const Vertex &vertex : mesh.vertices)
		if (std::abs(glm::length(vertex.normal) - 1.0f) > 1e-5f)
			return false;
	return true;
}

// Each triangle faces the way its vertices' normals say: its winding agrees with its shading.
bool WindingMatchesNormals(const MeshData &mesh)
{
	for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
	{
		const Vertex &a = mesh.vertices[mesh.indices[triangle]];
		const Vertex &b = mesh.vertices[mesh.indices[triangle + 1]];
		const Vertex &c = mesh.vertices[mesh.indices[triangle + 2]];
		const glm::vec3 face = glm::cross(b.pos - a.pos, c.pos - a.pos);
		if (glm::dot(face, a.normal + b.normal + c.normal) <= 0.0f)
			return false;
	}
	return true;
}

bool IndicesInRange(const MeshData &mesh)
{
	for (const std::uint32_t index : mesh.indices)
		if (index >= mesh.vertices.size())
			return false;
	return mesh.indices.size() % 3 == 0;
}
} // namespace

TEST_CASE("Procedural: every closed shape is watertight, wound outwards, and of the right volume")
{
	const glm::vec3 color{1.0f};
	const double sphere = 4.0 / 3.0 * std::numbers::pi;
	struct Case
	{
		const char *name;
		MeshData mesh;
		double volume;
		// How close a faceted shape's volume comes to the smooth one's.
		double tolerance;
	};
	const std::vector<Case> cases{
	    {"uv sphere", UvSphere(64, color), sphere, 0.01},
	    {"box", Box(color), 8.0, 1e-9},
	    {"ico sphere", IcoSphere(4, color), sphere, 0.01},
	    {"cube sphere", CubeSphere(16, color), sphere, 0.01},
	    {"cylinder", Cylinder(64, color), 2.0 * std::numbers::pi, 0.01},
	};
	for (const Case &shape : cases)
	{
		CAPTURE(shape.name);
		CHECK(IndicesInRange(shape.mesh));
		CHECK(IsClosedAndOriented(shape.mesh));
		CHECK(NormalsAreUnit(shape.mesh));
		CHECK(WindingMatchesNormals(shape.mesh));
		const double volume = SignedVolume(shape.mesh);
		CHECK(volume > 0.0);
		CHECK(std::abs(volume - shape.volume) / shape.volume < shape.tolerance);
	}
}

TEST_CASE("Procedural: an ico sphere has 10 * 4^n + 2 shared vertices, all on the unit sphere")
{
	for (int subdivisions = 0; subdivisions <= 4; ++subdivisions)
	{
		CAPTURE(subdivisions);
		const MeshData mesh = IcoSphere(subdivisions, glm::vec3{1.0f});
		const std::size_t faces = std::size_t{20} << (2 * subdivisions);
		CHECK(mesh.vertices.size() == 10 * (std::size_t{1} << (2 * subdivisions)) + 2);
		CHECK(mesh.indices.size() == faces * 3);
		for (const Vertex &vertex : mesh.vertices)
			CHECK(std::abs(glm::length(vertex.pos) - 1.0f) < 1e-5f);
	}
	CHECK_THROWS_AS(IcoSphere(-1, glm::vec3{1.0f}), std::invalid_argument);
}

TEST_CASE("Procedural: a cube sphere's faces each cover 0 to 1 in texture space")
{
	const int segments = 8;
	const MeshData mesh = CubeSphere(segments, glm::vec3{1.0f});
	const auto columns = static_cast<std::size_t>(segments) + 1;
	const std::size_t perFace = columns * columns;
	REQUIRE(mesh.vertices.size() == 6 * perFace);
	CHECK(mesh.indices.size() == 6 * (columns - 1) * (columns - 1) * 6);
	for (std::size_t face = 0; face < 6; ++face)
	{
		glm::vec2 low{1.0f};
		glm::vec2 high{0.0f};
		for (std::size_t index = face * perFace; index < (face + 1) * perFace; ++index)
		{
			low = glm::min(low, mesh.vertices[index].texCoord);
			high = glm::max(high, mesh.vertices[index].texCoord);
			CHECK(std::abs(glm::length(mesh.vertices[index].pos) - 1.0f) < 1e-5f);
		}
		CHECK(low == glm::vec2{0.0f});
		CHECK(high == glm::vec2{1.0f});
	}
}

TEST_CASE("Procedural: a ring is a flat annulus with a face up and a face down")
{
	const std::size_t segments = 32;
	const float inner = 0.6f;
	const MeshData mesh = Ring(static_cast<int>(segments), inner, glm::vec3{1.0f});
	// Two faces of two rims, a column per segment and one for the seam; two triangles a segment
	// a face.
	CHECK(mesh.vertices.size() == 4 * (segments + 1));
	CHECK(mesh.indices.size() == 12 * segments);
	CHECK(IndicesInRange(mesh));
	CHECK(NormalsAreUnit(mesh));
	CHECK(WindingMatchesNormals(mesh));
	// The two faces cover each other exactly, wound opposite ways: they enclose nothing.
	CHECK(std::abs(SignedVolume(mesh)) < 1e-6);

	// The area seen from above is the annulus's, to within the polygon's faceting.
	double upArea = 0.0;
	for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
	{
		const Vertex &a = mesh.vertices[mesh.indices[triangle]];
		if (a.normal.y < 0.0f)
			continue;
		const glm::dvec3 ab{mesh.vertices[mesh.indices[triangle + 1]].pos - a.pos};
		const glm::dvec3 ac{mesh.vertices[mesh.indices[triangle + 2]].pos - a.pos};
		upArea += glm::cross(ab, ac).y / 2.0;
	}
	const double expected = std::numbers::pi * (1.0 - inner * inner);
	CHECK(std::abs(upArea - expected) / expected < 0.01);
	for (const Vertex &vertex : mesh.vertices)
	{
		const float radius = glm::length(vertex.pos);
		CHECK(vertex.pos.y == 0.0f);
		CHECK((std::abs(radius - 1.0f) < 1e-5f || std::abs(radius - inner) < 1e-5f));
	}

	CHECK_THROWS_AS(Ring(32, 1.0f, glm::vec3{1.0f}), std::invalid_argument);
	CHECK_THROWS_AS(Ring(2, 0.5f, glm::vec3{1.0f}), std::invalid_argument);
}

TEST_CASE("Procedural: every shape a scene can name round-trips through its name")
{
	for (const Shape shape :
	     {Shape::Sphere, Shape::Box, Shape::IcoSphere, Shape::CubeSphere, Shape::Ring, Shape::Cylinder})
	{
		Shape parsed{};
		REQUIRE(ParseShape(ShapeName(shape), parsed));
		CHECK(parsed == shape);
		CHECK(std::string{ShapeNames()}.find(ShapeName(shape)) != std::string::npos);
	}
	Shape parsed{};
	CHECK_FALSE(ParseShape("torus", parsed));
}
