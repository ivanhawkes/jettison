#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Jettison::Geometry
{
class GltfFile;

// How a model's meshes are given levels of detail.
struct LodSettings
{
	// Five levels halving each time take the coarsest to a sixteenth of the finest's triangles.
	static constexpr std::uint32_t kDefaultMaxLevels = 5;

	static constexpr float kDefaultReduction = 0.5f;

	// Five percent of the mesh's extent: past it a level no longer looks like the mesh at any
	// distance it would be chosen at.
	static constexpr float kDefaultMaxError = 0.05f;

	// The most levels a mesh is given, its finest included; at least 1.
	std::uint32_t maxLevels{kDefaultMaxLevels};

	// A level's target triangles as a share of the level before it's, in (0, 1).
	float reduction{kDefaultReduction};

	// The most a level's surface may stray from the finest's, as a share of the mesh's extent: a
	// level that cannot reach its target within it stops short of it.
	float maxError{kDefaultMaxError};
};

// The engine's cooked GLB for a model (docs/rendering.md, "The cooked model"): every mesh with its
// normals, its tangents by MikkTSpace where it has normals and texture coordinates and no tangents,
// its vertices in vertex-cache and fetch order, its levels of detail as runs of primitives, its
// attributes quantised (KHR_mesh_quantization) and its vertex and index views compressed
// (EXT_meshopt_compression); the nodes, skins, clips, materials, textures and images kept as they
// are. cookKey is written into the default scene's extras, read back by GltfFile::CookKey.
//
// The same source, settings and key give the same bytes. Throws std::runtime_error naming the source
// for what it cannot cook: a primitive that is not triangles or has morph targets, a model with no
// scene, an image that is not embedded, and settings out of range.
std::vector<std::byte> CookModel(
    const GltfFile &source, const LodSettings &settings, std::string_view cookKey);
} // namespace Jettison::Geometry
