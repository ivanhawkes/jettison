#include "AppArguments.h"

#include <iostream>
#include <string>
#include <system_error>

namespace Jettison::Renderer
{
namespace
{
// The asset tree the build stages beside the executable. /proc/self/exe wins over argv[0] where
// it exists: argv[0] is the name the caller used, so a symlink - a launcher, an installed
// wrapper - points the default at a directory the assets are not in.
std::filesystem::path DefaultAssetDirectory(
    const char *argv0, const std::filesystem::path &assetSubdirectory)
{
	std::filesystem::path executable = std::filesystem::absolute(argv0);
#ifdef __linux__
	std::error_code error;
	const auto actual = std::filesystem::canonical("/proc/self/exe", error);
	if (!error)
		executable = actual;
#endif
	return executable.parent_path() / assetSubdirectory;
}

// The usage line names the flags this file reads, and goes to the stream the failure report
// uses, so the two halves of one exit path arrive together.
void PrintUsage(const char *argv0)
{
	std::cerr << "Usage: " << argv0
	          << " [--smoke-test] [--assets <directory>] [--scene <file>]"
	             " [--save-scene <file>]\n";
}
} // namespace

std::optional<AppArguments> ParseAppArguments(
    int argc, char **argv, const std::filesystem::path &assetSubdirectory)
{
	AppArguments arguments;
	arguments.assetDirectory = DefaultAssetDirectory(argv[0], assetSubdirectory);
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--smoke-test")
			arguments.smokeTest = true;
		else if (argument == "--assets" && index + 1 < argc)
			arguments.assetDirectory = std::filesystem::absolute(argv[++index]);
		else if (argument == "--scene" && index + 1 < argc)
			arguments.scenePath = std::filesystem::absolute(argv[++index]);
		else if (argument == "--save-scene" && index + 1 < argc)
			arguments.saveScenePath = std::filesystem::absolute(argv[++index]);
		else
		{
			PrintUsage(argv[0]);
			return std::nullopt;
		}
	}
	if (arguments.scenePath.empty())
		arguments.scenePath = arguments.assetDirectory / "scene" / "main.yaml";
	return arguments;
}
} // namespace Jettison::Renderer
