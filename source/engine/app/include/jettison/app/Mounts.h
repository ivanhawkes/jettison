#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/sim/SaveGame.h>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::App
{
// Where the user's mods are, under the user's data directory: each a directory, or a pack of one
// (vfs/Pack.h), its identifier the directory's name or the pack's without ".pack".
inline constexpr std::string_view kModDirectory = "mods";

// The player's list of mods, beside settings.yaml in the configuration directory.
inline constexpr std::string_view kModListFile = "mods.yaml";
inline constexpr std::uint32_t kModFormatVersion = 1;

// A mod's own description is a Host::PackageDescription. A mod without one mounts as a mod did before
// phase 12a, under its identifier and of no version.
struct ModEntry
{
	std::string mod;
	bool enabled{true};
};

// The player's list (mods.yaml), in mount order, a later mod winning where two change one file:
// written by the game's menu, and by MountAssets as it adds a mod it finds and the list does not
// name. Registered as "Shell.ModList", at version 1.
struct ModList
{
	std::vector<ModEntry> mods;
};

// The shell's mounts, bottom to top: the engine's assets (a directory or a pack) as "engine"; the
// game's content over them (likewise), unless there is none, as "base"; the user's mods
// in the data directory, as "mod:<identifier>"; the user's data directory itself, writable, as "user",
// where saves go. The mods are in the list's order and only those it enables; a mod found and not
// listed is added at the end, enabled, in the order of the identifiers' bytes, and the list written
// again, so a directory dropped in works and keeps its place; one listed and missing is reported and
// keeps its place. Without a list's path, every mod found mounts in the order of the identifiers'
// bytes. It is logged, with what each mount is.
//
// One bad mod does not stop the game: a mod that cannot be mounted (a damaged pack), whose
// description does not read, is for a newer engine, or needs a mod not mounted before it, is
// reported and left out; anything else in the mods directory is reported and ignored; a list that
// does not read is reported, every mod found mounts, and the list is left as it is for its player to
// mend. Throws as Vfs::FileSystem::AddMount does for the assets, the content or the user's
// directory, and for content whose description (Host::PackageDescription) does not read or is for a
// newer engine: a game does not start on content it cannot run.
//
// Returns the mods mounted, in order, with their versions, which a save records.
//
// The user mount's root is the data directory, so its mods are under "mods/" in the virtual tree
// too; nothing reads there.
std::vector<Sim::ModVersion> MountAssets(
    Vfs::FileSystem &files, const std::filesystem::path &assets, const std::filesystem::path &content,
    const std::filesystem::path &userData, const std::filesystem::path &modList = {});

void RegisterModTypes();
} // namespace Jettison::App
