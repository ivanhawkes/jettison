#include "Saves.h"

#include <algorithm>
#include <format>
#include <stdexcept>

#include <jettison/celestial/Galaxy.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::App
{
namespace
{
constexpr std::string_view kSaveDirectory = "saves";
constexpr std::string_view kSaveExtension = ".save";
constexpr Serialise::FormatVersion kShellVersion{1};

void RegisterShellState()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<ShellState>("Shell.State")
		    .Field<&ShellState::ship>("ship", Attribute::Saved)
		    .Field<&ShellState::station>("station", Attribute::Saved)
		    .Field<&ShellState::port>("port", Attribute::Saved)
		    .Field<&ShellState::shipSystem>("shipSystem", Attribute::Saved)
		    .Field<&ShellState::stationBody>("stationBody", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

bool IsSaveName(std::string_view name)
{
	return !name.empty() && name.size() <= 64 &&
	       std::ranges::all_of(
	           name,
	           [](char c)
	           {
		           return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
		                  c == '-' || c == '_';
	           });
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

std::string SavePath(std::string_view name)
{
	if (!IsSaveName(name))
		throw std::invalid_argument{
		    std::format("'{}' is not a save's name: up to 64 letters, digits, '-' and '_'", name)};
	return std::format("{}/{}{}", kSaveDirectory, name, kSaveExtension);
}

std::vector<std::string> ListSaves(const Vfs::FileSystem &fileSystem)
{
	std::vector<std::string> names;
	const std::string prefix = std::string{kSaveDirectory} + '/';
	for (const std::string &path : fileSystem.Enumerate(kSaveDirectory))
	{
		if (!path.ends_with(kSaveExtension))
			continue;
		const std::string name =
		    path.substr(prefix.size(), path.size() - prefix.size() - kSaveExtension.size());
		if (IsSaveName(name))
			names.push_back(name);
	}
	return names;
}

std::vector<std::uint64_t> UnknownSystems(const entt::registry &world, const Celestial::Galaxy &galaxy)
{
	std::vector<std::uint64_t> known;
	known.reserve(galaxy.systems.size());
	for (const Celestial::GalaxySystem &system : galaxy.systems)
		known.push_back(Sim::SystemId(system.name));
	std::ranges::sort(known);

	std::vector<std::uint64_t> unknown;
	for (const auto [entity, in] : world.view<const Sim::InSystem>().each())
		if (in.system != Sim::kNoSystem && !std::ranges::binary_search(known, in.system))
			unknown.push_back(in.system);
	std::ranges::sort(unknown);
	const auto duplicates = std::ranges::unique(unknown);
	unknown.erase(duplicates.begin(), duplicates.end());
	return unknown;
}
} // namespace Jettison::App
