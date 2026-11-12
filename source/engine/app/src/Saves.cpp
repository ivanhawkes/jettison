#include "Saves.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <stdexcept>

#include <jettison/celestial/Galaxy.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::App
{
namespace
{
constexpr std::string_view kSaveDirectory = "saves";
constexpr std::string_view kSaveExtension = ".save";
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

void RegisterSavesScreenTypes()
{
	static const bool registered = []
	{
		Reflect::RegisterType<SaveRow>("Shell.SaveRow")
		    .Field<&SaveRow::name>("name")
		    .Field<&SaveRow::system>("system")
		    .Field<&SaveRow::written>("written")
		    .Field<&SaveRow::step>("step")
		    .Field<&SaveRow::engine>("engine")
		    .Field<&SaveRow::problem>("problem");
		Reflect::RegisterType<SavesScreen>("Shell.SavesScreen").Field<&SavesScreen::saves>("saves");
		return true;
	}();
	(void)registered;
}

SavesScreen BuildSavesScreen(const Vfs::FileSystem &fileSystem)
{
	SavesScreen screen;
	for (const std::string &name : ListSaves(fileSystem))
	{
		SaveRow row;
		row.name = name;
		try
		{
			const auto bytes = fileSystem.Read(SavePath(name));
			const Sim::SaveInfo info =
			    Sim::SaveGame::ReadInfo(bytes ? *bytes : std::vector<std::byte>{});
			row.system = info.system.empty() ? "no system" : info.system;
			row.written = std::format(
			    "{:%Y-%m-%d %H:%M}", std::chrono::sys_seconds{std::chrono::seconds{info.writtenAt}});
			row.step = info.step;
			row.engine = info.engineVersion;
		}
		catch (const std::exception &error)
		{
			row.problem = error.what();
		}
		screen.saves.push_back(std::move(row));
	}
	return screen;
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
