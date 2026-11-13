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
// A mod's description, at its root.
inline constexpr std::string_view kModDescriptionFile = "mod.yaml";
inline constexpr std::uint32_t kModFormatVersion = 1;

// A mod's own description (mod.yaml). Registered with Reflect as "Shell.ModDescription", every field
// Saved, at version 1. A mod without one mounts as a mod did before phase 12a, under its identifier
// and of no version.
struct ModDescription
{
	// To show: its identifier is its directory's or pack's name.
	std::string name;
	// Its own version, which a save records it by ("version" is the file format's).
	std::string release;
	// The engine version it was written for (Core::EngineVersion(), "0.1.0"): one newer than this
	// build's is refused, and none is not checked.
	std::string engine;
	// The mods it needs, by identifier, each enabled and before it in the list; refused with the
	// reason when one is not, rather than the list reordered. "requires" in the file.
	std::vector<std::string> required;
};

// One mod in the player's list.
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

// The shell's mounts, bottom to top: the assets (a directory or a pack) as "base"; the user's mods
// in the data directory, as "mod:<identifier>"; the user's data directory itself, writable, as "user",
// where saves go. The mods are in the list's order and only those it enables; a mod found and not
// listed is added at the end, enabled, in the order of the identifiers' bytes, and the list written
// again,
// so a directory dropped in works and keeps its place; one listed and missing is reported and keeps
// its place. Without a list's path, every mod found mounts in the order of the identifiers' bytes. It
// is
// logged, with what each mount is.
//
// One bad mod does not stop the game: a mod that cannot be mounted (a damaged pack), whose
// description does not read, is for a newer engine, or needs a mod not mounted before it, is
// reported and left out; anything else in the mods directory is reported and ignored; a list that
// does not read is reported, every mod found mounts, and the list is left as it is for its player to
// mend. Throws as Vfs::FileSystem::AddMount does for the assets or the user's directory.
//
// Returns the mods mounted, in order, with their versions, which a save records.
//
// The user mount's root is the data directory, so its mods are under "mods/" in the virtual tree
// too; nothing reads there.
std::vector<Sim::ModVersion> MountAssets(
    Vfs::FileSystem &files, const std::filesystem::path &assets, const std::filesystem::path &userData,
    const std::filesystem::path &modList = {});

// Whether an engine version a mod names is this build's or older, by its numbers ("0.1.0"). Throws
// std::invalid_argument for one that is not up to three numbers between dots.
bool EngineAtLeast(std::string_view engine, std::string_view required);

void RegisterModTypes();
} // namespace Jettison::App
