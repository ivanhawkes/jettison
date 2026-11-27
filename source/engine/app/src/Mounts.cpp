#include <jettison/app/Mounts.h>

#include <jettison/core/File.h>
#include <jettison/core/Log.h>
#include <jettison/core/Version.h>
#include <jettison/host/Package.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/vfs/FileSystem.h>

#include <algorithm>
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

// The shell's own mounts' names, which a mod may not take: a session mounts a mod under its
// identifier, beside them.
constexpr std::string_view kEngineMount = "engine";
constexpr std::string_view kContentMount = "base";
constexpr std::string_view kUserMount = "user";
constexpr std::string_view kModPrefix = "mod:";


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


// The package's description, or an empty one for none; throws for one that does not read, is for a
// newer engine than this build, or is for the client alone and holds world content, and logs what it
// names.
Host::PackageDescription Describe(const std::filesystem::path &source, std::string_view mount)
{
	const Host::PackageDescription description =
	    Host::DescribePackage(source).value_or(Host::PackageDescription{});
	if (!description.engine.empty() && !Host::EngineAtLeast(Core::EngineVersion(), description.engine))
		throw std::runtime_error{std::format(
		    "it was written for engine {}, newer than this build's {}", description.engine,
		    Core::EngineVersion())};
	Host::CheckSide(description, source);
	if (!description.name.empty())
		Core::Log(
		    Core::Severity::Info, kMounts, "{} is {}{}", mount, description.name,
		    description.release.empty() ? "" : " " + description.release);
	return description;
}

} // namespace


void RegisterModTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Reflect::RegisterType<ModEntry>("Shell.ModEntry")
		    .Field<&ModEntry::mod>("mod", Attribute::Saved)
		    .Field<&ModEntry::enabled>("enabled", Attribute::Saved);
		Reflect::RegisterType<ModList>("Shell.ModList").Field<&ModList::mods>("mods", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


std::vector<Sim::ModVersion> MountAssets(
    Vfs::FileSystem &files, const std::filesystem::path &assets, const std::filesystem::path &content,
    const std::filesystem::path &userData, const std::filesystem::path &modList)
{
	RegisterModTypes();
	files.AddMount({std::string{kEngineMount}, assets});

	// The content's name, its description's identifier or the default, which a mod's requires names.
	std::string contentMount{kContentMount};
	if (!content.empty())
	{
		try
		{
			const Host::PackageDescription description = Describe(content, "The content");

			// Only the engine's assets are mounted before it, as Host::Session holds a source to.
			for (const std::string &required : description.required)
				if (required != kEngineMount)
					throw std::runtime_error{std::format(
					    "it requires the package {}, which is not mounted before it", required)};
			if (!description.identifier.empty())
			{
				if (description.identifier == kEngineMount || description.identifier == kUserMount ||
				    description.identifier.starts_with(kModPrefix))
					throw std::runtime_error{std::format(
					    "its identifier, {}, is one of the shell's own mount names", description.identifier)};
				contentMount = description.identifier;
			}
		}
		catch (const std::exception &problem)
		{
			throw std::runtime_error{
			    std::format("The content {} does not start: {}", content.string(), problem.what())};
		}
		files.AddMount({contentMount, content});
	}

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
			if (entry.mod == kEngineMount || entry.mod == contentMount || entry.mod == kUserMount)
				throw std::runtime_error{
				    std::format("its name is the shell's own mount, {}", entry.mod)};
			const Host::PackageDescription description =
			    Describe(*pPath, std::format("The mod {}", entry.mod));
			// A mod may require the content, by its name, or a mod mounted before it.
			for (const std::string &required : description.required)
				if (!(required == contentMount && !content.empty()) &&
				    std::ranges::find(mounted, required, &Sim::ModVersion::mod) == mounted.end())
					throw std::runtime_error{std::format(
					    "it requires the mod {}, which is not mounted before it", required)};
			files.AddMount({std::string{kModPrefix} + entry.mod, *pPath});
			mounted.push_back({entry.mod, description.release});
		}
		catch (const std::exception &problem)
		{
			Core::Log(
			    Core::Severity::Error, kMounts, "The mod {} is left out: {}", pPath->string(),
			    problem.what());
		}
	}

	files.AddMount({std::string{kUserMount}, userData, true});
	for (const Vfs::Mount &mount : files.Mounts())
		Core::Log(
		    Core::Severity::Info, kMounts, "{}: the {} {}", mount.name,
		    mount.writable ? "writable directory" : KindOf(mount.source), mount.source.string());
	return mounted;
}
} // namespace Jettison::App
