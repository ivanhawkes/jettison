#pragma once

#include <cstdint>
#include <span>
#include <string>

#include <jettison/geometry/Cook.h>

namespace Jettison::Cook
{
// How an image becomes KTX 2, as scripts/import-planet-textures.sh's `ktx create` line has it: Basis
// Universal UASTC, rate-distortion optimised, supercompressed by zstd, mipmapped.
struct TextureSettings
{
	// UASTC's level, the `--uastc-quality` of the import script, which is libktx's default.
	static constexpr std::uint32_t kDefaultQuality = 2;

	// KTX_PACK_UASTC_MAX_LEVEL.
	static constexpr std::uint32_t kMaxQuality = 4;

	static constexpr std::uint32_t kDefaultZstdLevel = 19;

	// zstd's own range.
	static constexpr std::uint32_t kMaxZstdLevel = 22;

	// 0 (fastest) to kMaxQuality (slowest).
	std::uint32_t quality{kDefaultQuality};

	// Rate-distortion optimisation: a file zstd compresses smaller, for a little quality.
	bool rdo{true};

	// 1 to kMaxZstdLevel.
	std::uint32_t zstdLevel{kDefaultZstdLevel};

	// A full chain of levels down to 1x1, each a box filter of the one above it.
	bool mipmaps{true};
};

// What a source is cooked with: the engine's defaults, then the game's cook.yaml at the source root,
// then the source's own settings file beside it (big-ship.gltf.yaml), each a patch over those before
// it (Serialise::YamlArchive::ReadLayered), so one reader and its errors serve all three:
//
//   version: 1
//   lod: {maxLevels: 3, reduction: 0.5, maxError: 0.02}
//   textures: {quality: 2, rdo: true, zstdLevel: 19, mipmaps: true}
struct Settings
{
	Geometry::LodSettings lod;
	TextureSettings textures;
};

// A layer's name, which its errors and the cook key carry, and its text.
struct SettingsLayer
{
	std::string name;
	std::string text;
};

// The engine's defaults as text, the first layer of every source's settings.
SettingsLayer DefaultSettingsLayer();

// The settings the layers give. Throws std::runtime_error naming the layer for a key it does not know,
// a value of the wrong kind, or a value out of range: the first layer whose patch puts one out of
// range is the one named.
Settings ReadSettings(std::span<const SettingsLayer> layers);
} // namespace Jettison::Cook
