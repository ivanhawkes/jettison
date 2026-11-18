#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::App
{
// The detail of a save's info (Sim::SaveInfo::details) the saves' list shows, where a game writes
// one: the place it was saved in. `system`, as the space sample names it and the saves already
// written have it.
inline constexpr std::string_view kPlaceDetail = "system";

// Where a save of that name lives on the user mount: "saves/<name>.save". A name is letters, digits,
// '-' and '_', so it is one segment of a virtual path on any file system; throws
// std::invalid_argument for one that is not, saying so.
std::string SavePath(std::string_view name);

// The names of the saves the mounts hold, sorted.
std::vector<std::string> ListSaves(const Vfs::FileSystem &fileSystem);

// The saved-games screen's data (assets/ui/saves.rml), which the UI's "saves" model is bound to as
// "screen": a row a save, read from the front of its file (Sim::SaveGame::ReadInfo), and one that does
// not read listed with the reason. Registered with Reflect, for the data model to read.
struct SaveRow
{
	std::string name;
	std::string system;
	// When it was written, in UTC: "2026-11-12 09:38".
	std::string written;
	std::uint64_t step{0};
	std::string engine;
	// Why the save cannot be read, or empty.
	std::string problem;

	bool operator==(const SaveRow &) const = default;
};
struct SavesScreen
{
	std::vector<SaveRow> saves;

	bool operator==(const SavesScreen &) const = default;
};

void RegisterSavesScreenTypes();

// The saves the mounts hold, by name.
SavesScreen BuildSavesScreen(const Vfs::FileSystem &fileSystem);

} // namespace Jettison::App
