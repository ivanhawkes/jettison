#include "Mounts.h"

#include <jettison/core/Log.h>
#include <jettison/vfs/FileSystem.h>

#include <algorithm>
#include <exception>
#include <string>
#include <system_error>
#include <vector>

namespace Jettison::App
{
namespace
{
constexpr Core::Category kMounts{"mounts"};
constexpr std::string_view kPackExtension = ".pack";

const char *KindOf(const std::filesystem::path &source)
{
	std::error_code error;
	return std::filesystem::is_directory(source, error) ? "directory" : "pack";
}
} // namespace

void MountAssets(
    Vfs::FileSystem &files, const std::filesystem::path &assets, const std::filesystem::path &userData)
{
	files.AddMount({"base", assets});

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
	    mods, {}, [](const std::filesystem::path &mod) { return mod.filename().string(); });
	for (const std::filesystem::path &mod : mods)
	{
		try
		{
			files.AddMount({"mod:" + mod.filename().string(), mod});
		}
		catch (const std::exception &problem)
		{
			Core::Log(
			    Core::Severity::Error, kMounts, "The mod {} is left out: {}", mod.string(),
			    problem.what());
		}
	}

	files.AddMount({"user", userData, true});
	for (const Vfs::Mount &mount : files.Mounts())
		Core::Log(
		    Core::Severity::Info, kMounts, "{}: the {} {}", mount.name,
		    mount.writable ? "writable directory" : KindOf(mount.source), mount.source.string());
}
} // namespace Jettison::App
