#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Cook
{
// Part of every cook key, so a change to what the cook writes cooks every source again.
inline constexpr std::uint32_t kCookVersion = 2;

// The game's settings, at the source root (Settings.h).
inline constexpr std::string_view kGameSettingsName = "cook.yaml";

// The cook record, at the output root: each source by its path, with its cook key, its outputs and
// the files it references.
inline constexpr std::string_view kRecordName = "cook-record.yaml";

// What a run did, by paths relative to the trees' roots.
struct CookReport
{
	std::vector<std::string> cooked;
	std::vector<std::string> skipped;
	std::vector<std::string> deleted;

	// Each names the file it refuses and why; the run failed when there is any.
	std::vector<std::string> refusals;
};

// Throws std::runtime_error naming both trees when the output is the source, lies inside it or holds
// it, by their weakly canonical paths: the cook would read its own outputs as sources, and a watch
// would see each record it writes as a change and cook forever. CookTree and WatchTree call it first.
void CheckTrees(const std::filesystem::path &source, const std::filesystem::path &output);

// Cooks the source tree into the output tree once (docs/dense-geometry-and-ray-tracing.md, "The
// cook"). A source's output is at its relative path: a model (.gltf, .glb) as the engine's GLB
// (Geometry::CookModel), its images KTX 2 inside it, with its collision file
// (docs/physics.md, "A model's collision shape"); an image (.png, .jpg, .jpeg, .tga) that no model
// references as .ktx2; anything else copied. A settings file (cook.yaml at the root, or a source's
// name with .yaml after it) is no output, nor is a file a model references. Hidden files and
// directories (a leading dot) are not sources.
//
// A source whose cook key, the hash of its bytes, of each file it references, of its settings layers
// and of kCookVersion, matches the record's, and whose outputs are there, is skipped. An output the
// record names whose source has gone is deleted; no other file in the output is touched. Each output
// is written durably, to a partial file renamed into place (Core::WriteFileDurably), and the record
// last.
//
// A source the cook cannot take is refused, naming it, and the rest are cooked: a name that breaks
// the content-file rule (docs/coding-standards.md, "Content files"), a model that does not read, a
// reference out of the model's directory, bad settings, two sources with one output. A refused
// source's outputs from an earlier run are kept. Throws std::runtime_error, before anything is cooked,
// for trees CheckTrees refuses, a source that is not a directory or a record that does not read.
CookReport CookTree(const std::filesystem::path &source, const std::filesystem::path &output);
} // namespace Jettison::Cook
