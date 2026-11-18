#include "ShellState.h"

#include <algorithm>

#include <jettison/celestial/Galaxy.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Sample
{
namespace
{
// Version 2 since 9c, which took the station's docking port out of it as the docks took docking from
// the shell. A version 1 section is of a world that holds the shell's port, which this build does not
// register, so its save is refused whole, and the section with it, rather than migrated.
constexpr Serialise::FormatVersion kShellVersion{2};


void RegisterShellState()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<ShellState>("Shell.State")
		    .Field<&ShellState::ship>("ship", Attribute::Saved)
		    .Field<&ShellState::station>("station", Attribute::Saved)
		    .Field<&ShellState::shipSystem>("shipSystem", Attribute::Saved)
		    .Field<&ShellState::stationBody>("stationBody", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace


std::vector<std::byte> WriteShellState(const ShellState &state)
{
	RegisterShellState();
	return Serialise::BinaryArchive::Write(state, kShellVersion);
}


ShellState ReadShellState(std::span<const std::byte> bytes)
{
	RegisterShellState();
	return Serialise::BinaryArchive::Read<ShellState>(bytes, kShellVersion);
}


std::vector<std::uint64_t> UnknownSystems(const entt::registry &world, const Celestial::Galaxy &galaxy)
{
	std::vector<std::uint64_t> known;
	known.reserve(galaxy.systems.size());
	for (const Celestial::GalaxySystem &system : galaxy.systems)
		known.push_back(Sim::FrameId(system.name));
	std::ranges::sort(known);

	std::vector<std::uint64_t> unknown;
	for (const auto [entity, in] : world.view<const Sim::InFrame>().each())
		if (in.frame != Sim::kNoFrame && !std::ranges::binary_search(known, in.frame))
			unknown.push_back(in.frame);
	std::ranges::sort(unknown);
	const auto duplicates = std::ranges::unique(unknown);
	unknown.erase(duplicates.begin(), duplicates.end());
	return unknown;
}
} // namespace Jettison::Sample
