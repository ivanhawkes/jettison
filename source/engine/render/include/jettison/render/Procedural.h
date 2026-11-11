#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include <jettison/render/Mesh.h>
#include <jettison/scene/Shape.h>

namespace Jettison::Render
{
// Geometry made rather than loaded: a mesh's vertices and its triangle list, counter-clockwise
// seen from outside, with normals, texture coordinates and one vertex colour. The planets and
// stations the large-world scene shows are these, at levels of detail (GenerateLods).
struct MeshData
{
	std::vector<Vertex> vertices;
	std::vector<std::uint32_t> indices;
};

// A sphere of radius one, `segments` around and segments / 2 from pole to pole. A seam of
// duplicated vertices runs pole to pole, so the texture coordinates wrap.
MeshData UvSphere(int segments, const glm::vec3 &color);
// A cube from -1 to 1, flat-shaded: four vertices to a face.
MeshData Box(const glm::vec3 &color);
// An icosahedron with each triangle split into four, `subdivisions` times, pushed out to the unit
// sphere: 10 * 4^n + 2 vertices, every one shared, and triangles of nearly one size, which a UV
// sphere's poles do not have. Being welded, it has no seam to wrap texture coordinates across:
// they are the spherical ones, and the triangles that straddle the antimeridian interpolate them
// the long way round. It is for triplanar materials, which read no texture coordinates.
MeshData IcoSphere(int subdivisions, const glm::vec3 &color);
// A cube of `segments` by `segments` quads a face, pushed out to the unit sphere by the mapping
// that keeps the cells closest to one size (x * sqrt(1 - y^2/2 - z^2/2 + y^2 z^2/3), and so on).
// Each face has vertices of its own and texture coordinates from 0 to 1 across it: the face is
// the unit a planet's terrain tiles will subdivide. The faces' edge vertices coincide.
MeshData CubeSphere(int segments, const glm::vec3 &color);
// A flat annulus in the XZ plane, outer radius one, `segments` around: a planet's rings. It is two
// sided, with a face up and a face down that share no vertices, so each has its own normal. The
// texture coordinates run around in u and from the inner edge (0) to the outer (1) in v.
MeshData Ring(int segments, float innerRadius, const glm::vec3 &color);
// A cylinder of radius one from y = -1 to 1, `segments` around, with flat caps. The side has a seam
// of duplicated vertices so u wraps; the caps have vertices of their own, for their normals.
MeshData Cylinder(int segments, const glm::vec3 &color);
// One level of a shape's detail: the mesh, and its geometric error, the furthest its surface strays
// from the shape it stands for, in the unit shape's own units. The renderer draws the coarsest level
// whose error, scaled by the transform and projected, covers less than its threshold in pixels.
struct MeshLod
{
	MeshData mesh;
	float error{0.0f};
};

// A shape's levels of detail, finest first, each about half the resolution of the one before; the
// finest is the resolution the scene drew every shape at before levels of detail. A box has the one
// level, which is exact. `innerRadius` is the ring's, from 0 to 1, and the other shapes ignore it.
std::vector<MeshLod> GenerateLods(Scene::Shape shape, const glm::vec3 &color, float innerRadius = 0.5f);

// How far inside the unit sphere a mesh whose vertices lie on it reaches: one less the nearest any of
// its triangles' planes passes the centre. It bounds the gap between a flat triangle and the sphere
// over it, which is where a tessellated sphere's error is largest.
float SphereTessellationError(const MeshData &mesh);
} // namespace Jettison::Render
