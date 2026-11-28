#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/vfs/Pack.h>

namespace Jettison::Host
{
inline constexpr std::string_view kPackageDescriptionFile = "mod.yaml";
inline constexpr std::uint32_t kPackageFormatVersion = 1;

// A mod's mount name is this prefix and its identifier ("mod:copper") wherever mods are mounted: the
// shell's mounts, its session's and a server's, so one mod is one package on every end.
inline constexpr std::string_view kModMountPrefix = "mod:";

// The directories that hold the world, which a server reads: definitions, scripts and the collision
// files baked from models (Physics::ReadCollisionFile), so a server collides as a client does and
// reads no model. The rest of a package (models, textures, scenes, UI, sounds, string tables) is
// presentation.
inline constexpr std::array<std::string_view, 3> kWorldDirectories{"defs", "scripts", "collision"};

// Which side of a networked game a package is for ("side" in mod.yaml): both, the default, or the
// client alone, as a cosmetic mod is. A server leaves a client's package out, and its players need not
// match it, so it may hold no world content.
enum class Side : std::uint8_t
{
	Both,
	Client,
};

// What a package (a directory or a pack) says of itself in mod.yaml at its root.
struct PackageDescription
{
	std::string name;

	// Its own version, which a save records it by ("version" in the file is the format's).
	std::string release;

	// The oldest engine version the package runs on, "major.minor.patch"; empty for any.
	std::string engine;

	// The packages that must be mounted before it, "requires" in the file: a mod by its identifier
	// ("copper", mounted as "mod:copper"), the engine's assets or a game's content by its mount name
	// ("engine", "manifest"). A mod named by its mount name is refused as the description is read.
	std::vector<std::string> required;

	Side side{Side::Both};

	// The name a game's content mounts under, which a mod's requires names it by ("manifest"); empty
	// for the shell's default, "base". A mod's own is its directory's or pack's name, so a mod's is
	// not read.
	std::string identifier;
};

// Reflects PackageDescription. Safe to call any number of times, by any number of sessions.
void RegisterPackageTypes();

// The description at the root of a directory or pack, read through a mount of it alone, or none for
// a package with no mod.yaml. Throws for a source that cannot be mounted or a description that does
// not read, one requiring a mod by its mount name among them. A field the file omits is left empty:
// whether one is required is the caller's policy.
std::optional<PackageDescription> DescribePackage(const std::filesystem::path &source);

// Whether the mount is the package a requires entry names (PackageDescription::required): the mount
// of that name, or the mod of that identifier.
bool Fulfils(std::string_view mount, std::string_view required);

// Whether the version is the required one or later. Throws std::invalid_argument for either that is
// not one to three numbers separated by dots.
bool EngineAtLeast(std::string_view engine, std::string_view required);

// Whether a directory or pack holds a file under any of kWorldDirectories. Throws as
// DescribePackage does for a source that cannot be mounted.
bool HoldsWorld(const std::filesystem::path &source);

// Throws std::runtime_error, saying why, for a description that cannot be mounted as it says: one
// for the client alone whose source holds world content.
void CheckSide(const PackageDescription &description, const std::filesystem::path &source);

// Packs what a server reads of a package directory, its mod.yaml and its world directories, and
// leaves its presentation out, for a dedicated server's bundle. Throws std::runtime_error for a
// package for the client alone or one with no world, and as Vfs::WritePack does.
Vfs::PackSummary WriteServerPack(
    const std::filesystem::path &directory, const std::filesystem::path &pack,
    int level = Vfs::PackOptions{}.level);
} // namespace Jettison::Host
