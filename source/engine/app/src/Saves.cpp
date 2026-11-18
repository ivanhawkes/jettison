#include "Saves.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <stdexcept>

#include <jettison/reflect/Reflect.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::App
{
namespace
{
constexpr std::string_view kSaveDirectory = "saves";
constexpr std::string_view kSaveExtension = ".save";


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
			const std::string_view system = info.Detail(kPlaceDetail);
			row.system = system.empty() ? "no system" : system;
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


} // namespace Jettison::App
