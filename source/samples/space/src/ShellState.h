#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Celestial
{
struct Galaxy;
}

namespace Jettison::Sample
{
// The space layer's state beyond the world's (SpaceLayer), saved in a section of the save game
// (Sim::SaveGame): which of the world's entities are its ship and the station it is bound to, and
// where. Whether the ship is docked is the world's (Dock::Docking). Registered with Reflect as
// "Shell.State", every field Saved; the section is a binary archive of that format, at version 2,
// under the same name. The names are the shell's, which wrote the section before the sample did
// (G01), and are kept so the saves already written load.
struct ShellState
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	std::uint64_t shipSystem{0};
	std::string stationBody;
};

inline constexpr std::string_view kShellSection = "Shell.State";

// The details of a save's info (Sim::SaveInfo::details): the galaxy's virtual path, which a load must
// match, and the star system shown, which the saves' list shows (App's kPlaceDetail).
inline constexpr std::string_view kGalaxyDetail = "galaxy";
inline constexpr std::string_view kSystemDetail = "system";

std::vector<std::byte> WriteShellState(const ShellState &state);

// Throws FormatError or FormatVersionError for a section that does not fit.
ShellState ReadShellState(std::span<const std::byte> bytes);

// The systems the world's entities are in (Sim::InFrame) that the galaxy does not have, sorted:
// a save of another galaxy, or of one a mod has since changed. Entities on a lane are in none.
std::vector<std::uint64_t> UnknownSystems(const entt::registry &world, const Celestial::Galaxy &galaxy);
} // namespace Jettison::Sample
