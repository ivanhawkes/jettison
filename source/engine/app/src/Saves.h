#pragma once

#include <entt/entt.hpp>

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
namespace Jettison::Celestial
{
struct Galaxy;
}

namespace Jettison::App
{
// The shell's own state beyond the world's, saved in a section of the save game as a game's would
// be (Sim::SaveGame): which of the world's entities are its ship, the station it holds station by
// and the station's docking port, and where. Whether the ship is docked is the world's (Docked).
// Registered with Reflect as "Shell.State", every field Saved; the section is a binary archive of
// that format, at version 1, under the same name.
struct ShellState
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	entt::entity port{entt::null};
	std::uint64_t shipSystem{0};
	std::string stationBody;
};

inline constexpr std::string_view kShellSection = "Shell.State";

std::vector<std::byte> WriteShellState(const ShellState &state);
// Throws FormatError or FormatVersionError for a section that does not fit.
ShellState ReadShellState(std::span<const std::byte> bytes);

// Where a save of that name lives on the user mount: "saves/<name>.save". A name is letters, digits,
// '-' and '_', so it is one segment of a virtual path on any file system; throws
// std::invalid_argument for one that is not, saying so.
std::string SavePath(std::string_view name);
// The names of the saves the mounts hold, sorted.
std::vector<std::string> ListSaves(const Vfs::FileSystem &fileSystem);

// The systems the world's entities are in (Sim::InSystem) that the galaxy does not have, sorted:
// a save of another galaxy, or of one a mod has since changed. Entities on a lane are in none.
std::vector<std::uint64_t> UnknownSystems(const entt::registry &world, const Celestial::Galaxy &galaxy);
} // namespace Jettison::App
