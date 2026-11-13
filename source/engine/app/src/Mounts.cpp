#include "Mounts.h"

#include <jettison/core/File.h>
#include <jettison/core/Log.h>
#include <jettison/core/Version.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Jettison::App
{
namespace
{
constexpr Core::Category kMounts{"mounts"};
constexpr std::string_view kPackExtension = ".pack";

// A mod's identifier: its directory's name, or its pack's without the extension, so a mod is the same
// mod to a save and to another's requirement whether it is packed or not.
std::string IdentifierOf(const std::filesystem::path &mod)
{
	return mod.extension() == kPackExtension ? mod.stem().string() : mod.filename().string();
}

const char *KindOf(const std::filesystem::path &source)
{
	std::error_code error;
	return std::filesystem::is_directory(source, error) ? "directory" : "pack";
}

// The mods directory's mods, in the order of their identifiers' bytes, one of each: a directory and a
// pack of one identifier are reported, and the directory kept.
std::vector<std::filesystem::path> FindMods(const std::filesystem::path &userData)
{
	std::vector<std::filesystem::path> mods;
	std::error_code error;
	const std::filesystem::path modDirectory = userData / kModDirectory;
	for (std::filesystem::directory_iterator entry{modDirectory, error};
	     !error && entry != std::filesystem::directory_iterator{}; entry.increment(error))
	{
		std::error_code entryError;
		if (entry->is_directory(entryError) ||
		    (entry->is_regular_file(entryError) && entry->path().extension() == kPackExtension))
			mods.push_back(entry->path());
		else
			Core::Log(
			    Core::Severity::Warning, kMounts,
			    "{} is not a mod, which is a directory or a {} file, and is ignored",
			    entry->path().string(), kPackExtension);
	}
	std::ranges::sort(
	    mods, {}, [](const std::filesystem::path &mod)
	    { return std::pair{IdentifierOf(mod), mod.extension() == kPackExtension}; });
	std::vector<std::filesystem::path> unique;
	for (std::filesystem::path &mod : mods)
	{
		if (!unique.empty() && IdentifierOf(unique.back()) == IdentifierOf(mod))
			Core::Log(
			    Core::Severity::Warning, kMounts, "{} is a second mod {}, and is ignored", mod.string(),
			    IdentifierOf(mod));
		else
			unique.push_back(std::move(mod));
	}
	return unique;
}

// The list, or nothing for a list that is not there or does not read, which is said.
std::optional<ModList> ReadList(const std::filesystem::path &path, bool &readable)
{
	readable = true;
	std::error_code error;
	if (!std::filesystem::exists(path, error))
		return std::nullopt;
	std::ifstream file{path, std::ios::binary};
	const std::string text{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
	try
	{
		auto list = Serialise::YamlArchive::Read<ModList>(text, {kModFormatVersion});
		std::vector<std::string_view> seen;
		for (const ModEntry &entry : list.mods)
		{
			if (entry.mod.empty() || std::ranges::find(seen, entry.mod) != seen.end())
				throw Serialise::FormatError{
				    std::format("it names the mod '{}' twice, or a mod with no name", entry.mod)};
			seen.push_back(entry.mod);
		}
		return list;
	}
	catch (const std::exception &problem)
	{
		readable = false;
		Core::Log(
		    Core::Severity::Error, kMounts,
		    "{} does not read, so every mod found is mounted and the list is left as it is: {}",
		    path.string(), problem.what());
		return std::nullopt;
	}
}

// The description at the mod's root, read through a mount of the mod alone, or none for a mod with
// none. Throws for one that does not read, or that the mod cannot be mounted to read.
ModDescription Describe(const std::filesystem::path &mod)
{
	Vfs::FileSystem alone;
	alone.AddMount({"mod", mod});
	const std::optional<std::vector<std::byte>> bytes = alone.Read(kModDescriptionFile);
	if (!bytes)
		return {};
	const std::string_view text{reinterpret_cast<const char *>(bytes->data()), bytes->size()};
	return Serialise::YamlArchive::Read<ModDescription>(text, {kModFormatVersion});
}

std::array<unsigned, 3> VersionNumbers(std::string_view version)
{
	std::array<unsigned, 3> numbers{};
	std::size_t index = 0;
	const char *pAt = version.data();
	const char *pEnd = version.data() + version.size();
	while (true)
	{
		if (index == numbers.size())
			throw std::invalid_argument{std::format("'{}' is not a version of three numbers", version)};
		const auto [pNext, error] = std::from_chars(pAt, pEnd, numbers[index++]);
		if (error != std::errc{} || pNext == pAt)
			throw std::invalid_argument{std::format("'{}' is not a version of numbers", version)};
		if (pNext == pEnd)
			return numbers;
		if (*pNext != '.')
			throw std::invalid_argument{std::format("'{}' is not a version of numbers", version)};
		pAt = pNext + 1;
	}
}
} // namespace

void RegisterModTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<ModDescription>("Shell.ModDescription")
		    .Field<&ModDescription::name>("name", Attribute::Saved)
		    .Field<&ModDescription::release>("release", Attribute::Saved)
		    .Field<&ModDescription::engine>("engine", Attribute::Saved)
		    .Field<&ModDescription::required>("requires", Attribute::Saved);
		Reflect::RegisterType<ModEntry>("Shell.ModEntry")
		    .Field<&ModEntry::mod>("mod", Attribute::Saved)
		    .Field<&ModEntry::enabled>("enabled", Attribute::Saved);
		Reflect::RegisterType<ModList>("Shell.ModList").Field<&ModList::mods>("mods", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

bool EngineAtLeast(std::string_view engine, std::string_view required)
{
	return VersionNumbers(engine) >= VersionNumbers(required);
}

std::vector<Sim::ModVersion> MountAssets(
    Vfs::FileSystem &files, const std::filesystem::path &assets, const std::filesystem::path &userData,
    const std::filesystem::path &modList)
{
	RegisterModTypes();
	files.AddMount({"base", assets});

	const std::vector<std::filesystem::path> found = FindMods(userData);
	const auto pathOf = [&found](std::string_view mod) -> const std::filesystem::path *
	{
		const auto path = std::ranges::find(found, mod, IdentifierOf);
		return path == found.end() ? nullptr : &*path;
	};

	// The order: the list's, then each mod it does not name.
	bool readable = true;
	ModList list = modList.empty() ? ModList{} : ReadList(modList, readable).value_or(ModList{});
	std::vector<std::string> added;
	for (const std::filesystem::path &mod : found)
	{
		const std::string name = IdentifierOf(mod);
		if (std::ranges::find(list.mods, name, &ModEntry::mod) == list.mods.end())
		{
			list.mods.push_back({name, true});
			added.push_back(name);
		}
	}
	if (!modList.empty() && readable && !added.empty())
	{
		try
		{
			Core::WriteFileDurably(modList, Serialise::YamlArchive::Write(list, {kModFormatVersion}));
			for (const std::string &name : added)
				Core::Log(
				    Core::Severity::Info, kMounts, "The mod {} is added to {}", name, modList.string());
		}
		catch (const std::exception &problem)
		{
			Core::Log(
			    Core::Severity::Error, kMounts, "The list of mods is not written: {}", problem.what());
		}
	}

	std::vector<Sim::ModVersion> mounted;
	for (const ModEntry &entry : list.mods)
	{
		const std::filesystem::path *pPath = pathOf(entry.mod);
		if (pPath == nullptr)
		{
			Core::Log(
			    Core::Severity::Warning, kMounts,
			    "The mod {} is in the list and not in {}; it keeps its place", entry.mod,
			    (userData / kModDirectory).string());
			continue;
		}
		if (!entry.enabled)
		{
			Core::Log(Core::Severity::Info, kMounts, "The mod {} is disabled", entry.mod);
			continue;
		}
		try
		{
			const ModDescription description = Describe(*pPath);
			if (!description.engine.empty() &&
			    !EngineAtLeast(Core::EngineVersion(), description.engine))
				throw std::runtime_error{std::format(
				    "it was written for engine {}, newer than this build's {}", description.engine,
				    Core::EngineVersion())};
			for (const std::string &required : description.required)
				if (std::ranges::find(mounted, required, &Sim::ModVersion::mod) == mounted.end())
					throw std::runtime_error{std::format(
					    "it requires the mod {}, which is not mounted before it", required)};
			files.AddMount({"mod:" + entry.mod, *pPath});
			mounted.push_back({entry.mod, description.release});
			if (!description.name.empty())
				Core::Log(
				    Core::Severity::Info, kMounts, "The mod {} is {}{}", entry.mod, description.name,
				    description.release.empty() ? "" : " " + description.release);
		}
		catch (const std::exception &problem)
		{
			Core::Log(
			    Core::Severity::Error, kMounts, "The mod {} is left out: {}", pPath->string(),
			    problem.what());
		}
	}

	files.AddMount({"user", userData, true});
	for (const Vfs::Mount &mount : files.Mounts())
		Core::Log(
		    Core::Severity::Info, kMounts, "{}: the {} {}", mount.name,
		    mount.writable ? "writable directory" : KindOf(mount.source), mount.source.string());
	return mounted;
}
} // namespace Jettison::App
