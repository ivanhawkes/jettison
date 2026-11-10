#include <jettison/render/Procedural.h>

#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace Jettison::Render
{
namespace
{
// Enough for a planet's limb to read as round from low orbit: chords 0.45 km short of the
// surface on an Earth-sized body, about a pixel from 400 km at 1080p.
constexpr int kSphereSegments = 256;
} // namespace

bool ParseShape(std::string_view name, Shape &shape)
{
	if (name == "sphere")
		shape = Shape::Sphere;
	else if (name == "box")
		shape = Shape::Box;
	else
		return false;
	return true;
}

std::string_view ShapeName(Shape shape)
{
	return shape == Shape::Sphere ? "sphere" : "box";
}

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
	// Each face: its normal and the two axes across it, ordered so the corners run anticlockwise
	// seen from outside.
	const std::array<std::array<glm::vec3, 3>, 6> faces{{
	    {glm::vec3{1, 0, 0}, glm::vec3{0, 0, -1}, glm::vec3{0, 1, 0}},
	    {glm::vec3{-1, 0, 0}, glm::vec3{0, 0, 1}, glm::vec3{0, 1, 0}},
	    {glm::vec3{0, 1, 0}, glm::vec3{1, 0, 0}, glm::vec3{0, 0, -1}},
	    {glm::vec3{0, -1, 0}, glm::vec3{1, 0, 0}, glm::vec3{0, 0, 1}},
	    {glm::vec3{0, 0, 1}, glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0}},
	    {glm::vec3{0, 0, -1}, glm::vec3{-1, 0, 0}, glm::vec3{0, 1, 0}},
	}};
	for (const auto &[normal, across, up] : faces)
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

MeshData Generate(Shape shape, const glm::vec3 &color)
{
	return shape == Shape::Sphere ? UvSphere(kSphereSegments, color) : Box(color);
}
} // namespace Jettison::Render
