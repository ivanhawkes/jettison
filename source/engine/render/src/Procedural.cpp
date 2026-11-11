#include <jettison/render/Procedural.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace Jettison::Render
{
using Scene::Shape;

namespace
{
// Enough for a planet's limb to read as round from low orbit: chords 0.45 km short of the
// surface on an Earth-sized body, about a pixel from 400 km at 1080p.
constexpr int kSphereSegments = 256;
// 10 242 vertices, edges 1.2 degrees of arc long: about the UV sphere's chord at the equator.
constexpr int kIcoSubdivisions = 6;
// 64 x 64 cells a face: 1.4 degrees of arc a cell at a face's middle.
constexpr int kCubeSphereSegments = 64;
constexpr int kRingSegments = 256;
constexpr int kCylinderSegments = 64;
// Levels of detail a shape other than a box has, each half the resolution of the one before.
constexpr int kLodLevels = 5;

// The faces of the cube from -1 to 1: each face's normal and the two axes across it, ordered so the
// corners (-1, -1), (1, -1), (1, 1), (-1, 1) run anticlockwise seen from outside.
constexpr std::array<std::array<glm::vec3, 3>, 6> kCubeFaces{{
    {glm::vec3{1, 0, 0}, glm::vec3{0, 0, -1}, glm::vec3{0, 1, 0}},
    {glm::vec3{-1, 0, 0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 0}},
    {glm::vec3{0, 1, 0}, glm::vec3{1, 0, 0}, glm::vec3{0, 0, -1}},
    {glm::vec3{0, -1, 0}, glm::vec3{1, 0, 0}, glm::vec3{0, 0, 1}},
    {glm::vec3{0, 0, 1}, glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0}},
    {glm::vec3{0, 0, -1}, glm::vec3{-1, 0, 0}, glm::vec3{0, 1, 0}},
}};

Vertex MakeVertex(
    const glm::vec3 &position, const glm::vec3 &normal, const glm::vec2 &texCoord,
    const glm::vec3 &color)
{
	Vertex vertex{};
	vertex.pos = position;
	vertex.normal = normal;
	vertex.texCoord = texCoord;
	vertex.color = color;
	return vertex;
}

// Texture coordinates for a point on the unit sphere, as UvSphere lays them out: u around from +X
// towards -Z, v from the north pole (0) to the south (1).
glm::vec2 SphericalTexCoord(const glm::vec3 &normal)
{
	const double azimuth = std::atan2(-static_cast<double>(normal.z), static_cast<double>(normal.x));
	const double u = azimuth / (2.0 * std::numbers::pi);
	return {
	    static_cast<float>(u < 0.0 ? u + 1.0 : u),
	    static_cast<float>(
	        std::acos(std::clamp(static_cast<double>(normal.y), -1.0, 1.0)) / std::numbers::pi)};
}

void CheckSegments(int segments, int minimum, const char *shape)
{
	if (segments < minimum)
		throw std::invalid_argument(
		    std::string{"A "} + shape + " needs " + std::to_string(minimum) + " segments or more");
}
} // namespace

MeshData UvSphere(int segments, const glm::vec3 &color)
{
	if (segments < 4 || segments % 2 != 0)
		throw std::invalid_argument("A UV sphere needs an even number of segments, four or more");
	const int rings = segments / 2;
	MeshData mesh;
	mesh.vertices.reserve(static_cast<std::size_t>(segments + 1) * static_cast<std::size_t>(rings + 1));
	for (int ring = 0; ring <= rings; ++ring)
	{
		const double polar = std::numbers::pi * ring / rings;
		for (int segment = 0; segment <= segments; ++segment)
		{
			const double azimuth = 2.0 * std::numbers::pi * segment / segments;
			const glm::vec3 normal{
			    static_cast<float>(std::sin(polar) * std::cos(azimuth)),
			    static_cast<float>(std::cos(polar)),
			    static_cast<float>(-std::sin(polar) * std::sin(azimuth))};
			Vertex vertex{};
			vertex.pos = normal;
			vertex.normal = normal;
			vertex.color = color;
			vertex.texCoord = {
			    static_cast<float>(segment) / static_cast<float>(segments),
			    static_cast<float>(ring) / static_cast<float>(rings)};
			mesh.vertices.push_back(vertex);
		}
	}
	const auto at = [segments](int ring, int segment)
	{ return static_cast<std::uint32_t>(ring * (segments + 1) + segment); };
	for (int ring = 0; ring < rings; ++ring)
		for (int segment = 0; segment < segments; ++segment)
		{
			// The pole rows collapse to a point: their degenerate half of the quad is left out.
			if (ring != 0)
				mesh.indices.insert(
				    mesh.indices.end(),
				    {at(ring, segment), at(ring + 1, segment), at(ring, segment + 1)});
			if (ring != rings - 1)
				mesh.indices.insert(
				    mesh.indices.end(),
				    {at(ring, segment + 1), at(ring + 1, segment), at(ring + 1, segment + 1)});
		}
	return mesh;
}

MeshData Box(const glm::vec3 &color)
{
	MeshData mesh;
	for (const auto &[normal, across, up] : kCubeFaces)
	{
		const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
		for (const glm::vec2 corner :
		     {glm::vec2{-1, -1}, glm::vec2{1, -1}, glm::vec2{1, 1}, glm::vec2{-1, 1}})
		{
			Vertex vertex{};
			vertex.pos = normal + across * corner.x + up * corner.y;
			vertex.normal = normal;
			vertex.color = color;
			vertex.texCoord = (corner + 1.0f) * 0.5f;
			mesh.vertices.push_back(vertex);
		}
		mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
	}
	return mesh;
}

MeshData IcoSphere(int subdivisions, const glm::vec3 &color)
{
	if (subdivisions < 0 || subdivisions > 9)
		throw std::invalid_argument("An ico sphere takes from 0 to 9 subdivisions");
	// The twelve corners are the cyclic permutations of (0, +-1, +-golden ratio).
	const auto golden = static_cast<float>((1.0 + std::sqrt(5.0)) / 2.0);
	std::vector<glm::vec3> positions{
	    {-1, golden, 0}, {1, golden, 0}, {-1, -golden, 0}, {1, -golden, 0},
	    {0, -1, golden}, {0, 1, golden}, {0, -1, -golden}, {0, 1, -golden},
	    {golden, 0, -1}, {golden, 0, 1}, {-golden, 0, -1}, {-golden, 0, 1},
	};
	for (glm::vec3 &position : positions)
		position = glm::normalize(position);
	std::vector<std::uint32_t> indices{
	    0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
	    4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
	    6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1,
	};

	for (int level = 0; level < subdivisions; ++level)
	{
		// Each edge's midpoint is made once, by whichever of its two triangles reaches it first.
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> midpoints;
		const auto midpoint = [&](std::uint32_t a, std::uint32_t b)
		{
			const auto key = std::minmax(a, b);
			if (const auto found = midpoints.find(key); found != midpoints.end())
				return found->second;
			positions.push_back(glm::normalize(positions[a] + positions[b]));
			const auto index = static_cast<std::uint32_t>(positions.size() - 1);
			midpoints.emplace(key, index);
			return index;
		};
		std::vector<std::uint32_t> next;
		next.reserve(indices.size() * 4);
		for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3)
		{
			const std::uint32_t a = indices[triangle];
			const std::uint32_t b = indices[triangle + 1];
			const std::uint32_t c = indices[triangle + 2];
			const std::uint32_t ab = midpoint(a, b);
			const std::uint32_t bc = midpoint(b, c);
			const std::uint32_t ca = midpoint(c, a);
			next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
		}
		indices = std::move(next);
	}

	MeshData mesh;
	mesh.vertices.reserve(positions.size());
	for (const glm::vec3 &position : positions)
		mesh.vertices.push_back(MakeVertex(position, position, SphericalTexCoord(position), color));
	mesh.indices = std::move(indices);
	return mesh;
}

MeshData CubeSphere(int segments, const glm::vec3 &color)
{
	CheckSegments(segments, 1, "cube sphere");
	MeshData mesh;
	const auto columns = static_cast<std::uint32_t>(segments + 1);
	for (const auto &[normal, across, up] : kCubeFaces)
	{
		const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
		for (int row = 0; row <= segments; ++row)
			for (int column = 0; column <= segments; ++column)
			{
				const glm::vec2 at{
				    static_cast<float>(column) / static_cast<float>(segments),
				    static_cast<float>(row) / static_cast<float>(segments)};
				const glm::vec3 cube =
				    normal + across * (at.x * 2.0f - 1.0f) + up * (at.y * 2.0f - 1.0f);
				const glm::vec3 squared = cube * cube;
				const glm::vec3 sphere{
				    cube.x *
				        std::sqrt(
				            1.0f - squared.y / 2.0f - squared.z / 2.0f + squared.y * squared.z / 3.0f),
				    cube.y *
				        std::sqrt(
				            1.0f - squared.z / 2.0f - squared.x / 2.0f + squared.z * squared.x / 3.0f),
				    cube.z *
				        std::sqrt(
				            1.0f - squared.x / 2.0f - squared.y / 2.0f + squared.x * squared.y / 3.0f)};
				// The mapping lands on the sphere to within float rounding; the normalise is for the
				// normal, which has to be unit length.
				const glm::vec3 unit = glm::normalize(sphere);
				mesh.vertices.push_back(MakeVertex(unit, unit, at, color));
			}
		for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(segments); ++row)
			for (std::uint32_t column = 0; column < static_cast<std::uint32_t>(segments); ++column)
			{
				const std::uint32_t corner = base + row * columns + column;
				mesh.indices.insert(
				    mesh.indices.end(), {corner, corner + 1, corner + columns + 1, corner,
				                         corner + columns + 1, corner + columns});
			}
	}
	return mesh;
}

MeshData Ring(int segments, float innerRadius, const glm::vec3 &color)
{
	CheckSegments(segments, 3, "ring");
	// NaN is refused too.
	if (std::isnan(innerRadius) || innerRadius < 0.0f || innerRadius >= 1.0f)
		throw std::invalid_argument("A ring's inner radius is from 0 up to, not including, 1");
	MeshData mesh;
	const auto columns = static_cast<std::uint32_t>(segments + 1);
	// The face up, then the face down: the same rim, the other normal and the other winding.
	for (const float side : {1.0f, -1.0f})
	{
		const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
		const glm::vec3 normal{0.0f, side, 0.0f};
		for (const float radius : {innerRadius, 1.0f})
			for (int segment = 0; segment <= segments; ++segment)
			{
				const double azimuth = 2.0 * std::numbers::pi * segment / segments;
				const glm::vec3 position{
				    radius * static_cast<float>(std::cos(azimuth)), 0.0f,
				    -radius * static_cast<float>(std::sin(azimuth))};
				mesh.vertices.push_back(MakeVertex(
				    position, normal,
				    {static_cast<float>(segment) / static_cast<float>(segments),
				     radius == 1.0f ? 1.0f : 0.0f},
				    color));
			}
		for (std::uint32_t segment = 0; segment < static_cast<std::uint32_t>(segments); ++segment)
		{
			const std::uint32_t inner = base + segment;
			const std::uint32_t outer = inner + columns;
			// Azimuth runs anticlockwise seen from +Y, so inner, outer, next outer faces up.
			if (side > 0.0f)
				mesh.indices.insert(
				    mesh.indices.end(), {inner, outer, outer + 1, inner, outer + 1, inner + 1});
			else
				mesh.indices.insert(
				    mesh.indices.end(), {inner, outer + 1, outer, inner, inner + 1, outer + 1});
		}
	}
	return mesh;
}

MeshData Cylinder(int segments, const glm::vec3 &color)
{
	CheckSegments(segments, 3, "cylinder");
	MeshData mesh;
	const auto rim = [segments](int segment)
	{
		const double azimuth = 2.0 * std::numbers::pi * segment / segments;
		return glm::vec3{
		    static_cast<float>(std::cos(azimuth)), 0.0f, -static_cast<float>(std::sin(azimuth))};
	};

	// The side: a bottom row and a top row, a column per segment and one more for the seam.
	const auto columns = static_cast<std::uint32_t>(segments + 1);
	for (const float y : {-1.0f, 1.0f})
		for (int segment = 0; segment <= segments; ++segment)
		{
			const glm::vec3 around = rim(segment);
			mesh.vertices.push_back(MakeVertex(
			    around + glm::vec3{0.0f, y, 0.0f}, around,
			    {static_cast<float>(segment) / static_cast<float>(segments), y > 0.0f ? 0.0f : 1.0f},
			    color));
		}
	for (std::uint32_t segment = 0; segment < static_cast<std::uint32_t>(segments); ++segment)
	{
		const std::uint32_t bottom = segment;
		const std::uint32_t top = segment + columns;
		mesh.indices.insert(mesh.indices.end(), {bottom, bottom + 1, top + 1, bottom, top + 1, top});
	}

	// The caps: a centre and a rim each, the texture laid flat across the disc.
	for (const float y : {1.0f, -1.0f})
	{
		const auto centre = static_cast<std::uint32_t>(mesh.vertices.size());
		const glm::vec3 normal{0.0f, y, 0.0f};
		mesh.vertices.push_back(MakeVertex(normal, normal, {0.5f, 0.5f}, color));
		for (int segment = 0; segment < segments; ++segment)
		{
			const glm::vec3 around = rim(segment);
			mesh.vertices.push_back(MakeVertex(
			    around + normal, normal, {0.5f + 0.5f * around.x, 0.5f + 0.5f * around.z}, color));
		}
		for (std::uint32_t segment = 0; segment < static_cast<std::uint32_t>(segments); ++segment)
		{
			const std::uint32_t here = centre + 1 + segment;
			const std::uint32_t next =
			    centre + 1 + (segment + 1) % static_cast<std::uint32_t>(segments);
			if (y > 0.0f)
				mesh.indices.insert(mesh.indices.end(), {centre, here, next});
			else
				mesh.indices.insert(mesh.indices.end(), {centre, next, here});
		}
	}
	return mesh;
}

float SphereTessellationError(const MeshData &mesh)
{
	double nearest = 1.0;
	for (std::size_t triangle = 0; triangle + 2 < mesh.indices.size(); triangle += 3)
	{
		const glm::dvec3 a{mesh.vertices[mesh.indices[triangle]].pos};
		const glm::dvec3 b{mesh.vertices[mesh.indices[triangle + 1]].pos};
		const glm::dvec3 c{mesh.vertices[mesh.indices[triangle + 2]].pos};
		const glm::dvec3 normal = glm::cross(b - a, c - a);
		// A UV sphere's pole triangles have two corners at the pole and no area.
		const double area = glm::length(normal);
		if (area < 1e-12)
			continue;
		nearest = std::min(nearest, std::abs(glm::dot(normal / area, a)));
	}
	return static_cast<float>(1.0 - nearest);
}

namespace
{
// A ring's and a cylinder's error is the chord across one segment of the outer circle, short of it
// at the chord's middle by 1 - cos(pi / segments).
MeshLod RoundLevel(MeshData mesh, int segments)
{
	const auto error = static_cast<float>(1.0 - std::cos(std::numbers::pi / segments));
	return {std::move(mesh), error};
}

MeshLod SphereLevel(MeshData mesh)
{
	const float error = SphereTessellationError(mesh);
	return {std::move(mesh), error};
}

// Each level halves the segments around, which about quadruples the error: a chord's sagitta grows
// with the square of the angle it spans.
MeshLod Level(Shape shape, int level, const glm::vec3 &color, float innerRadius)
{
	switch (shape)
	{
		case Shape::Sphere: return SphereLevel(UvSphere(kSphereSegments >> level, color));
		case Shape::Box: return {Box(color), 0.0f};
		case Shape::IcoSphere: return SphereLevel(IcoSphere(kIcoSubdivisions - level, color));
		case Shape::CubeSphere: return SphereLevel(CubeSphere(kCubeSphereSegments >> level, color));
		case Shape::Ring:
			return RoundLevel(Ring(kRingSegments >> level, innerRadius, color), kRingSegments >> level);
		case Shape::Cylinder:
			return RoundLevel(Cylinder(kCylinderSegments >> level, color), kCylinderSegments >> level);
	}
	throw std::invalid_argument("Unknown procedural shape");
}
} // namespace

std::vector<MeshLod> GenerateLods(Shape shape, const glm::vec3 &color, float innerRadius)
{
	// Five levels take a cylinder from 64 segments to 4 and a sphere from 256 to 16: the coarsest is
	// a few pixels across where it is drawn.
	const int levels = shape == Shape::Box ? 1 : kLodLevels;
	std::vector<MeshLod> lods;
	lods.reserve(static_cast<std::size_t>(levels));
	for (int level = 0; level < levels; ++level)
		lods.push_back(Level(shape, level, color, innerRadius));
	return lods;
}
} // namespace Jettison::Render
