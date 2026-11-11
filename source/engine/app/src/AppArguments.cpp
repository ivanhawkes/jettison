#include <jettison/app/AppArguments.h>

#include <charconv>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace Jettison::App
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

// A positive whole number, all of the text, or nothing.
std::optional<std::uint32_t> ParseCount(std::string_view text)
{
	std::uint32_t value = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end != text.data() + text.size() || value == 0)
		return std::nullopt;
	return value;
}

// The usage line names the flags this file reads, and goes to the stream the failure report
// uses, so the two halves of one exit path arrive together.
void PrintUsage(const char *argv0)
{
	std::cerr << "Usage: " << argv0
	          << " [--smoke-test] [--assets <directory>] [--scene <file> | --system <virtual path>]"
	             " [--save-scene <file>] [--benchmark <frames>] [--window <width>x<height>]"
	             " [--depth-prepass] [--galaxy <virtual path>] [--user-dir <directory>]\n";
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
		else if (argument == "--system" && index + 1 < argc)
			arguments.systemPath = argv[++index];
		else if (argument == "--galaxy" && index + 1 < argc)
			arguments.galaxyPath = argv[++index];
		else if (argument == "--save-scene" && index + 1 < argc)
			arguments.saveScenePath = std::filesystem::absolute(argv[++index]);
		else if (argument == "--depth-prepass")
			arguments.depthPrepass = true;
		else if (argument == "--user-dir" && index + 1 < argc)
			arguments.userDirectory = std::filesystem::absolute(argv[++index]);
		else if (argument == "--benchmark" && index + 1 < argc)
		{
			const auto frames = ParseCount(argv[++index]);
			if (!frames)
			{
				PrintUsage(argv[0]);
				return std::nullopt;
			}
			arguments.benchmarkFrames = *frames;
		}
		else if (argument == "--window" && index + 1 < argc)
		{
			const std::string_view size{argv[++index]};
			const std::size_t cross = size.find('x');
			const auto width =
			    cross == std::string_view::npos ? std::nullopt : ParseCount(size.substr(0, cross));
			const auto height =
			    cross == std::string_view::npos ? std::nullopt : ParseCount(size.substr(cross + 1));
			if (!width || !height)
			{
				PrintUsage(argv[0]);
				return std::nullopt;
			}
			arguments.windowWidth = width;
			arguments.windowHeight = height;
		}
		else
		{
			PrintUsage(argv[0]);
			return std::nullopt;
		}
	}
	if (!arguments.scenePath.empty() && !arguments.systemPath.empty())
	{
		PrintUsage(argv[0]);
		return std::nullopt;
	}
	if (arguments.scenePath.empty() && arguments.systemPath.empty())
		arguments.scenePath = arguments.assetDirectory / "scene" / "main.yaml";
	return arguments;
}
} // namespace Jettison::App
