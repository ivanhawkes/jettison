#include <fastgltf/core.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <jettison/core/File.h>
#include <jettison/core/Log.h>

// jettison_test_meshes <directory>: the dense test meshes, written as GLB files under <directory>, the
// test content's tree the cook reads (docs/dense-geometry-and-ray-tracing.md, "The cook"): two
// asteroids, displaced icospheres, and a station's kit of a corridor, a junction and a panel. Each
// has positions, texture coordinates and a material, and no normals, which the cook makes. The same
// bytes every run, in every build type: nothing random and nothing timed, and no contraction into FMAs
// (source/test/CMakeLists.txt).

using namespace Jettison;

namespace
{
constexpr Core::Category kMeshes{"test-meshes"};

constexpr int kUsageError = 2;

constexpr float kPi = std::numbers::pi_v<float>;

constexpr float kTau = 2.0f * kPi;

// Each subdivision splits every triangle in four, from the icosahedron's twenty.
constexpr int kLargeAsteroidSubdivisions = 6;

constexpr int kSmallAsteroidSubdivisions = 5;

// Lumps over the whole body, and finer relief over them, as fractions of the radius.
constexpr float kLumpAmplitude = 0.18f;

constexpr glm::vec3 kLumpFrequency{3.0f, 5.0f, 2.0f};

constexpr float kReliefAmplitude = 0.03f;

constexpr glm::vec3 kReliefFrequency{17.0f, 23.0f, 19.0f};

// A crater's rim, beyond its bowl: its height as a fraction of the bowl's depth, and its width as a
// fraction of the bowl's radius.
constexpr float kCraterRimHeight = 0.3f;

constexpr float kCraterRimWidth = 0.5f;

struct Crater
{
	glm::vec3 direction;

	// The bowl's angular radius, in radians, and its depth, a fraction of the asteroid's radius.
	float radius;
	float depth;
};

constexpr std::array kCraters{
    Crater{{0.6f, 0.7f, 0.2f}, 0.45f, 0.08f}, Crater{{-0.5f, 0.1f, 0.8f}, 0.3f, 0.06f},
    Crater{{0.1f, -0.9f, -0.3f}, 0.6f, 0.1f}, Crater{{-0.7f, -0.4f, -0.5f}, 0.2f, 0.04f}};

struct Asteroid
{
	std::string_view path;
	int subdivisions;

	// Metres, and the body's scale along each axis, so neither is a sphere.
	float radius;
	glm::vec3 stretch;

	// Moves the lumps, so the two bodies differ in shape as well as size.
	float phase;
};

// 81,920 and 20,480 triangles, so the cook's levels of detail have detail to take away.
constexpr std::array kAsteroids{
    Asteroid{
        "models/asteroids/asteroid-large.glb",
        kLargeAsteroidSubdivisions,
        40.0f,
        {1.0f, 0.8f, 1.3f},
        0.0f},
    Asteroid{
        "models/asteroids/asteroid-small.glb",
        kSmallAsteroidSubdivisions,
        12.0f,
        {1.2f, 0.9f, 0.9f},
        1.7f}};

// The station's kit, in metres: a corridor is a ribbed hull round an inner wall, open at both ends,
// which a junction crosses with a second; a panel is a plate of tiles parted by grooves. Their
// surfaces are cut finer than their shapes need, so the cook's levels have flat runs to merge.
constexpr std::string_view kCorridorPath = "models/station/station-corridor.glb";

constexpr std::string_view kJunctionPath = "models/station/station-junction.glb";

constexpr std::string_view kPanelPath = "models/station/station-panel.glb";

constexpr float kCorridorRadius = 1.5f;

constexpr float kCorridorLength = 6.0f;

constexpr float kWallThickness = 0.2f;

constexpr float kRibSpacing = 1.0f;

constexpr float kRibWidth = 0.2f;

constexpr float kRibHeight = 0.12f;

constexpr int kTubeSegmentsAround = 48;

constexpr int kTubeSegmentsAlong = 96;

constexpr float kPanelSize = 4.0f;

constexpr int kPanelCells = 128;

constexpr int kPanelTiles = 4;

constexpr float kPanelGrooveWidth = 0.06f;

constexpr float kPanelGrooveDepth = 0.04f;

struct Surface
{
	glm::vec4 colour;
	float metallic;
	float roughness;
};

constexpr Surface kRock{{0.42f, 0.38f, 0.34f, 1.0f}, 0.0f, 0.9f};

constexpr Surface kHull{{0.7f, 0.72f, 0.75f, 1.0f}, 0.6f, 0.45f};

struct Mesh
{
	std::vector<glm::vec3> positions;
	std::vector<glm::vec2> texCoords;
	std::vector<std::uint32_t> indices;
};


// A unit icosphere, its vertices shared, so the cook's normals are smooth across it.
Mesh Icosphere(int subdivisions)
{
	const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
	Mesh mesh;
	mesh.positions = {{-1, t, 0},  {1, t, 0},  {-1, -t, 0}, {1, -t, 0}, {0, -1, t},  {0, 1, t},
	                  {0, -1, -t}, {0, 1, -t}, {t, 0, -1},  {t, 0, 1},  {-t, 0, -1}, {-t, 0, 1}};
	mesh.indices = {0, 11, 5,  0, 5,  1, 0, 1, 7, 0, 7,  10, 0, 10, 11, 1, 5, 9, 5, 11,
	                4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3,  9,  4, 3,  4,  2, 3, 2, 6, 3,
	                6, 8,  3,  8, 9,  4, 9, 5, 2, 4, 11, 6,  2, 10, 8,  6, 7, 9, 8, 1};
	for (int subdivision = 0; subdivision < subdivisions; ++subdivision)
	{
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> midpoints;
		const auto midpoint = [&](std::uint32_t a, std::uint32_t b)
		{
			const auto [found, inserted] = midpoints.try_emplace(
			    std::minmax(a, b), static_cast<std::uint32_t>(mesh.positions.size()));
			if (inserted)
				mesh.positions.push_back((mesh.positions[a] + mesh.positions[b]) * 0.5f);
			return found->second;
		};
		std::vector<std::uint32_t> finer;
		finer.reserve(mesh.indices.size() * 4);
		for (std::size_t index = 0; index < mesh.indices.size(); index += 3)
		{
			const std::uint32_t a = mesh.indices[index];
			const std::uint32_t b = mesh.indices[index + 1];
			const std::uint32_t c = mesh.indices[index + 2];
			const std::uint32_t ab = midpoint(a, b);
			const std::uint32_t bc = midpoint(b, c);
			const std::uint32_t ca = midpoint(c, a);
			finer.insert(finer.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
		}
		mesh.indices = std::move(finer);
	}
	for (glm::vec3 &position : mesh.positions)
		position = glm::normalize(position);
	return mesh;
}


// The surface's height at a direction from the centre, as a fraction of the radius: a crater's bowl
// below it and its rim above, falling to nothing at the rim's edge.
float CraterHeight(const glm::vec3 &direction)
{
	float height = 0.0f;
	for (const Crater &crater : kCraters)
	{
		const float angle =
		    std::acos(std::clamp(glm::dot(direction, glm::normalize(crater.direction)), -1.0f, 1.0f));
		const float across = angle / crater.radius;
		if (across < 1.0f)
			height += crater.depth * ((across * across) - 1.0f);
		else if (across < 1.0f + kCraterRimWidth)
			height += crater.depth * kCraterRimHeight * (1.0f - ((across - 1.0f) / kCraterRimWidth));
	}
	return height;
}


Mesh MakeAsteroid(const Asteroid &asteroid)
{
	Mesh mesh = Icosphere(asteroid.subdivisions);
	for (glm::vec3 &position : mesh.positions)
	{
		const glm::vec3 direction = position;
		const glm::vec3 lumps = glm::sin((kLumpFrequency * direction) + asteroid.phase);
		const glm::vec3 relief = glm::sin((kReliefFrequency * direction) + asteroid.phase);
		const float height = 1.0f + (kLumpAmplitude * lumps.x * lumps.y * lumps.z) +
		                     (kReliefAmplitude * relief.x * relief.y * relief.z) +
		                     CraterHeight(direction);
		position = direction * (asteroid.radius * height) * asteroid.stretch;
		mesh.texCoords.emplace_back(
		    0.5f + (std::atan2(direction.z, direction.x) / kTau),
		    std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / kPi);
	}
	return mesh;
}


// A sheet of cells over the unit square, each vertex where place puts it, its texture coordinates
// the square's. Its front faces the side the cross product of place's u and v derivatives points to.
template <typename Place> void AddSheet(Mesh &mesh, int cellsU, int cellsV, const Place &place)
{
	const auto first = static_cast<std::uint32_t>(mesh.positions.size());
	for (int v = 0; v <= cellsV; ++v)
		for (int u = 0; u <= cellsU; ++u)
		{
			const glm::vec2 at{
			    static_cast<float>(u) / static_cast<float>(cellsU),
			    static_cast<float>(v) / static_cast<float>(cellsV)};
			mesh.positions.push_back(place(at));
			mesh.texCoords.push_back(at);
		}
	const auto row = static_cast<std::uint32_t>(cellsU + 1);
	for (int v = 0; v < cellsV; ++v)
		for (int u = 0; u < cellsU; ++u)
		{
			const std::uint32_t a =
			    first + (static_cast<std::uint32_t>(v) * row) + static_cast<std::uint32_t>(u);
			mesh.indices.insert(mesh.indices.end(), {a, a + 1, a + row + 1, a, a + row + 1, a + row});
		}
}


// A corridor along the rotation's Z, centred on the origin: its ribbed hull facing out, its inner
// wall facing in, and the rings that close the wall between them at each end.
void AddCorridor(Mesh &mesh, const glm::mat3 &rotation)
{
	const float inner = kCorridorRadius - kWallThickness;
	const auto ring = [&](float angle, float radius, float along)
	{
		return rotation * glm::vec3{
		                      radius * std::cos(angle), radius * std::sin(angle),
		                      (along - 0.5f) * kCorridorLength};
	};
	AddSheet(
	    mesh, kTubeSegmentsAround, kTubeSegmentsAlong,
	    [&](glm::vec2 at)
	    {
		    const float offset = std::fmod(at.y * kCorridorLength, kRibSpacing);
		    const bool onRib = std::abs(offset - (kRibSpacing / 2.0f)) < kRibWidth / 2.0f;
		    return ring(at.x * kTau, kCorridorRadius + (onRib ? kRibHeight : 0.0f), at.y);
	    });
	AddSheet(
	    mesh, kTubeSegmentsAround, kTubeSegmentsAlong,
	    [&](glm::vec2 at) { return ring((1.0f - at.x) * kTau, inner, at.y); });
	AddSheet(
	    mesh, kTubeSegmentsAround, 1,
	    [&](glm::vec2 at) { return ring(at.x * kTau, inner + (at.y * kWallThickness), 0.0f); });
	AddSheet(
	    mesh, kTubeSegmentsAround, 1, [&](glm::vec2 at)
	    { return ring((1.0f - at.x) * kTau, inner + (at.y * kWallThickness), 1.0f); });
}


Mesh MakeCorridor()
{
	Mesh mesh;
	AddCorridor(mesh, glm::mat3{1.0f});
	return mesh;
}


Mesh MakeJunction()
{
	Mesh mesh;
	AddCorridor(mesh, glm::mat3{1.0f});

	// The second corridor along X: Z turned a quarter about Y.
	const glm::mat3 across{glm::vec3{0, 0, -1}, glm::vec3{0, 1, 0}, glm::vec3{1, 0, 0}};
	AddCorridor(mesh, across);
	return mesh;
}


// A plate in the XZ plane facing up, centred on the origin, its tiles' edges grooved.
Mesh MakePanel()
{
	Mesh mesh;
	const float tile = kPanelSize / static_cast<float>(kPanelTiles);
	AddSheet(
	    mesh, kPanelCells, kPanelCells,
	    [&](glm::vec2 at)
	    {
		    const glm::vec2 offset = glm::mod(at * kPanelSize, tile);
		    const glm::vec2 toEdge = glm::min(offset, tile - offset);
		    const bool inGroove = std::min(toEdge.x, toEdge.y) < kPanelGrooveWidth / 2.0f;
		    return glm::vec3{
		        (at.x - 0.5f) * kPanelSize, inGroove ? -kPanelGrooveDepth : 0.0f,
		        (0.5f - at.y) * kPanelSize};
	    });
	return mesh;
}


template <typename T> void Append(std::vector<std::byte> &bytes, const std::vector<T> &values)
{
	const auto *pBytes = reinterpret_cast<const std::byte *>(values.data());
	bytes.insert(bytes.end(), pBytes, pBytes + (values.size() * sizeof(T)));
}


// The mesh as a GLB of one node, one primitive and one material, named as its file.
std::vector<std::byte> WriteGlb(const Mesh &mesh, const std::string &name, const Surface &surface)
{
	fastgltf::Asset asset;
	std::vector<std::byte> bytes;
	Append(bytes, mesh.positions);
	Append(bytes, mesh.texCoords);
	Append(bytes, mesh.indices);
	fastgltf::Buffer buffer{};
	buffer.byteLength = bytes.size();
	buffer.data = fastgltf::sources::Vector{std::move(bytes)};
	asset.buffers.push_back(std::move(buffer));

	const std::size_t positionBytes = mesh.positions.size() * sizeof(glm::vec3);
	const std::size_t texCoordBytes = mesh.texCoords.size() * sizeof(glm::vec2);
	const std::size_t indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
	for (const auto [offset, length] :
	     {std::pair{std::size_t{0}, positionBytes}, std::pair{positionBytes, texCoordBytes},
	      std::pair{positionBytes + texCoordBytes, indexBytes}})
	{
		fastgltf::BufferView view{};
		view.bufferIndex = 0;
		view.byteOffset = offset;
		view.byteLength = length;
		asset.bufferViews.push_back(std::move(view));
	}
	const auto accessor = [&](std::size_t view, std::size_t count, fastgltf::AccessorType type,
	                          fastgltf::ComponentType componentType)
	{
		fastgltf::Accessor made{};
		made.bufferViewIndex = view;
		made.count = count;
		made.type = type;
		made.componentType = componentType;
		asset.accessors.push_back(std::move(made));
	};
	accessor(0, mesh.positions.size(), fastgltf::AccessorType::Vec3, fastgltf::ComponentType::Float);
	accessor(1, mesh.texCoords.size(), fastgltf::AccessorType::Vec2, fastgltf::ComponentType::Float);
	accessor(
	    2, mesh.indices.size(), fastgltf::AccessorType::Scalar, fastgltf::ComponentType::UnsignedInt);

	// glTF requires a position accessor's bounds.
	glm::vec3 low = mesh.positions.front();
	glm::vec3 high = low;
	for (const glm::vec3 &position : mesh.positions)
	{
		low = glm::min(low, position);
		high = glm::max(high, position);
	}
	auto min = fastgltf::AccessorBoundsArray::ForType<double>(3);
	auto max = fastgltf::AccessorBoundsArray::ForType<double>(3);
	for (int axis = 0; axis < 3; ++axis)
	{
		min.data<double>()[axis] = double{low[axis]};
		max.data<double>()[axis] = double{high[axis]};
	}
	asset.accessors[0].min = std::move(min);
	asset.accessors[0].max = std::move(max);

	fastgltf::Material material{};
	material.name = name;
	material.pbrData.baseColorFactor = {
	    surface.colour.r, surface.colour.g, surface.colour.b, surface.colour.a};
	material.pbrData.metallicFactor = surface.metallic;
	material.pbrData.roughnessFactor = surface.roughness;
	asset.materials.push_back(std::move(material));

	fastgltf::Primitive primitive{};
	primitive.attributes = {{"POSITION", 0}, {"TEXCOORD_0", 1}};
	primitive.indicesAccessor = 2;
	primitive.materialIndex = 0;
	fastgltf::Mesh gltfMesh{};
	gltfMesh.name = name;
	gltfMesh.primitives.push_back(std::move(primitive));
	asset.meshes.push_back(std::move(gltfMesh));
	fastgltf::Node node{};
	node.name = name;
	node.meshIndex = 0;
	asset.nodes.push_back(std::move(node));
	asset.scenes.push_back({{0}, name});
	asset.defaultScene = 0;

	fastgltf::Exporter exporter;
	auto written = exporter.writeGltfBinary(asset);
	if (written.error() != fastgltf::Error::None)
		throw std::runtime_error(
		    std::format("{} does not export: {}", name, fastgltf::getErrorMessage(written.error())));
	return std::move(written.get().output);
}


void Write(
    const std::filesystem::path &root, std::string_view path, const Mesh &mesh, const Surface &surface)
{
	const std::filesystem::path file = root / path;
	Core::WriteFileDurably(file, WriteGlb(mesh, file.stem().string(), surface));
	Core::Log(
	    Core::Severity::Info, kMeshes, "{}: {} triangles", file.string(), mesh.indices.size() / 3);
}
} // namespace


int main(int argc, char **argv)
{
	if (argc != 2)
	{
		Core::Log(Core::Severity::Error, kMeshes, "Usage: jettison_test_meshes <directory>");
		return kUsageError;
	}
	try
	{
		const std::filesystem::path root{argv[1]};
		for (const Asteroid &asteroid : kAsteroids)
			Write(root, asteroid.path, MakeAsteroid(asteroid), kRock);
		Write(root, kCorridorPath, MakeCorridor(), kHull);
		Write(root, kJunctionPath, MakeJunction(), kHull);
		Write(root, kPanelPath, MakePanel(), kHull);
	}
	catch (const std::exception &error)
	{
		Core::Log(Core::Severity::Error, kMeshes, "{}", error.what());
		return 1;
	}
	return 0;
}
