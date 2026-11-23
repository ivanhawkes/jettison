#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/serialise/Archive.h>

namespace Jettison::Serialise
{
struct BinaryDocument;
template <typename Data> class Migrations;
using BinaryMigrations = Migrations<BinaryDocument>;
} // namespace Jettison::Serialise

namespace Jettison::Sim
{
class SimWorld;

// A mod a save was made with: its identifier (its directory's or pack's name) and its version, as
// its description gives it, or empty for a mod with none. Registered with Reflect as
// "Sim.ModVersion", every field Saved.
struct ModVersion
{
	std::string mod;
	std::string version;

	bool operator==(const ModVersion &) const = default;
};

// A detail of a game's in a save's info, for a list to show or a load to check, as the
// place it was saved in. Registered with Reflect as "Sim.SaveDetail", every field
// Saved.
struct SaveDetail
{
	std::string key;
	std::string value;

	bool operator==(const SaveDetail &) const = default;
};

// What a list of saves shows without loading one, and what a load checks first. Registered with
// Reflect, every field Saved, as "Sim.SaveInfo".
struct SaveInfo
{
	std::string name;
	// Core::EngineVersion() of the build that wrote it, for a report of a save that does not load;
	// what reads a save is its format versions, not this.
	std::string engineVersion;
	// When it was written, in seconds since the Unix epoch by the wall clock: the caller's to set,
	// since the simulation never reads the wall clock, and no part of the state.
	std::int64_t writtenAt{0};
	// The world's clock as it was saved.
	std::uint64_t step{0};
	double time{0.0};
	// The mods enabled as it was saved, in mount order: the caller's to set, as the mods are the
	// game's. A save from before phase 12a names none.
	std::vector<ModVersion> mods;
	// The game's own, by key: what a list shows of a save and a load checks first, read with the
	// rest of the info.
	std::vector<SaveDetail> details;

	// The value of the first detail of the key, or empty for none.
	std::string_view Detail(std::string_view key) const;
};

// What differs between the mods a save was made with and the mods running, a line each, for a load
// to say and go on: a mod saved with and not running, one running and not saved with, one at another
// version, and the same mods in another order. Empty when they are the same.
std::vector<std::string> ModDifferences(
    std::span<const ModVersion> saved, std::span<const ModVersion> running);

// A game's own state, beyond the world's (docs/scope.md: the game decides what a save holds beyond
// the engine's state): a binary archive in a format of the game's, with its own version and
// migrations, which the save holds and does not read.
struct SaveSection
{
	std::string name;
	std::vector<std::byte> data;
};

// A save game: one file, in its own versioned format ("Jettison.Save"), holding its SaveInfo, the
// world's save (SimWorld::Save, which is versioned and migrated on its own), and the game's
// sections. Each part keeps its own version, so a change to the world's layout is a step in the
// world's migrations and leaves the save game's format alone, and a game's section is migrated by
// the game. The file is written whole and durably through the VFS's user mount (Vfs::FileSystem::
// Write), so a save interrupted by a crash or a power cut leaves the one before it.
struct SaveGame
{
	static constexpr std::string_view kFormat = "Jettison.Save";
	static constexpr Serialise::FormatVersion kVersion{2};

	SaveInfo info;
	std::vector<std::byte> world;
	std::vector<SaveSection> sections;

	// The world's state and clock, this build's version, and a name; writtenAt, the mods and the
	// details are the caller's.
	static SaveGame Of(const SimWorld &world, std::string name);

	// Throws std::logic_error for a name that is empty or already taken.
	void AddSection(std::string name, std::vector<std::byte> data);

	const SaveSection *FindSection(std::string_view name) const;

	std::vector<std::byte> Write() const;

	// A save of any version the save game's migrations reach. Throws FormatError for data that is not
	// a save game or does not fit, and FormatVersionError for one of a version they do not, a later
	// build's among them; the world inside is read by SimWorld::Load, which migrates it in turn.
	static SaveGame Read(std::span<const std::byte> bytes);

	// The info alone, read from the front of the file, for a list of saves. Throws as Read does.
	static SaveInfo ReadInfo(std::span<const std::byte> bytes);

	// The save game format's chain: the current version alone until player testing (docs/decisions.md,
	// "Formats change in place until player testing").
	static const Serialise::BinaryMigrations &Migrations();
};

// Registers SaveInfo with Reflect. Idempotent; SaveGame calls it, from the main thread.
void RegisterSaveTypes();
} // namespace Jettison::Sim
