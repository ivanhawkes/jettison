#include <jettison/app/AppArguments.h>

#include <charconv>
#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace Jettison::App
{
namespace
{
// The asset tree the build stages beside the executable, or where there is none the pack of it a
// shipped build has. /proc/self/exe wins over argv[0] where it exists: argv[0] is the name the
// caller used, so a symlink - a launcher, an installed wrapper - points the default at a directory
// the assets are not in.
std::filesystem::path DefaultAssets(const char *argv0, const std::filesystem::path &assetSubdirectory)
{
	std::filesystem::path executable = std::filesystem::absolute(argv0);
#ifdef __linux__
	std::error_code error;
	const auto actual = std::filesystem::canonical("/proc/self/exe", error);
	if (!error)
		executable = actual;
#endif
	const std::filesystem::path directory =
	    (executable.parent_path() / assetSubdirectory).lexically_normal();
	std::filesystem::path pack = directory;
	pack += ".pack";
	std::error_code missing;
	return !std::filesystem::is_directory(directory, missing) &&
	               std::filesystem::is_regular_file(pack, missing)
	           ? pack
	           : directory;
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


// A positive number of seconds, all of the text, or nothing.
std::optional<double> ParseSeconds(std::string_view text)
{
	double value = 0.0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
	if (error != std::errc{} || end != text.data() + text.size() || !(value > 0.0))
		return std::nullopt;
	return value;
}


// "@<frame> <line>", "@<seconds>s <line>" or a line to run before the first frame; nothing for a
// when that is not a positive frame or time followed by a line.
std::optional<RunCommand> ParseRunCommand(std::string_view text)
{
	if (!text.starts_with('@'))
		return RunCommand{.line = std::string{text}, .frame = 0, .simulationSeconds = std::nullopt};
	const std::size_t space = text.find(' ');
	if (space == std::string_view::npos || space + 1 == text.size())
		return std::nullopt;
	const std::string_view when = text.substr(1, space - 1);
	RunCommand command;
	command.line = std::string{text.substr(space + 1)};
	if (when.ends_with('s'))
	{
		command.simulationSeconds = ParseSeconds(when.substr(0, when.size() - 1));
		if (!command.simulationSeconds)
			return std::nullopt;
	}
	else
	{
		const auto frame = ParseCount(when);
		if (!frame)
			return std::nullopt;
		command.frame = *frame;
	}
	return command;
}


// The usage line names the flags this file reads, and goes to the stream the failure report
// uses, so the two halves of one exit path arrive together.
void PrintUsage(const char *argv0)
{
	std::cerr << "Usage: " << argv0
	          << " [--smoke-test] [--assets <directory | pack>] [--content <directory | pack>] "
	             "[--scene <file>]"
	             " [--save-scene <file>] [--benchmark <frames>] [--window <width>x<height>]"
	             " [--depth-prepass] [--[no-]ambient-occlusion] [--bloom <strength>]"
	             " [--user-dir <directory>]"
	             " [--gpu <name>] [--no-sound] [--screenshot <file.png> [--after-frames <n>]]"
	             " [--frame-step <seconds>] [--run [@<frame> | @<seconds>s] <console command>]..."
	             " [--offscreen]\n";
}
} // namespace


std::optional<AppArguments> ParseAppArguments(
    int argc, char **argv, const std::filesystem::path &assetSubdirectory,
    const std::filesystem::path &contentSubdirectory)
{
	AppArguments arguments;
	arguments.assets = DefaultAssets(argv[0], assetSubdirectory);
	if (!contentSubdirectory.empty())
		arguments.content = DefaultAssets(argv[0], contentSubdirectory);
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--smoke-test")
			arguments.smokeTest = true;
		else if (argument == "--assets" && index + 1 < argc)
			arguments.assets = std::filesystem::absolute(argv[++index]);
		else if (argument == "--content" && index + 1 < argc)
			arguments.content = std::filesystem::absolute(argv[++index]);
		else if (argument == "--scene" && index + 1 < argc)
			arguments.scenePath = std::filesystem::absolute(argv[++index]);
		else if (argument == "--save-scene" && index + 1 < argc)
			arguments.saveScenePath = std::filesystem::absolute(argv[++index]);
		else if (argument == "--depth-prepass")
			arguments.depthPrepass = true;
		else if (argument == "--ambient-occlusion")
			arguments.ambientOcclusion = true;
		else if (argument == "--no-ambient-occlusion")
			arguments.ambientOcclusion = false;
		else if (argument == "--bloom" && index + 1 < argc)
		{
			const std::string_view text = argv[++index];
			float strength = 0.0f;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), strength);
			if (error != std::errc{} || end != text.data() + text.size() || !(strength >= 0.0f) ||
			    !(strength <= 1.0f))
			{
				PrintUsage(argv[0]);
				return std::nullopt;
			}
			arguments.bloom = strength;
		}
		else if (argument == "--no-sound")
			arguments.noSound = true;
		else if (argument == "--gpu" && index + 1 < argc)
			arguments.gpu = argv[++index];
		else if (argument == "--user-dir" && index + 1 < argc)
			arguments.userDirectory = std::filesystem::absolute(argv[++index]);
		else if (argument == "--screenshot" && index + 1 < argc)
			arguments.screenshotPath = std::filesystem::absolute(argv[++index]);
		else if (argument == "--run" && index + 1 < argc)
		{
			auto command = ParseRunCommand(argv[++index]);
			if (!command)
			{
				PrintUsage(argv[0]);
				return std::nullopt;
			}
			arguments.commands.push_back(std::move(*command));
		}
		else if (argument == "--offscreen")
			arguments.offscreen = true;
		else if (argument == "--after-frames" && index + 1 < argc)
		{
			const auto frames = ParseCount(argv[++index]);
			if (!frames)
			{
				PrintUsage(argv[0]);
				return std::nullopt;
			}
			arguments.screenshotAfterFrames = *frames;
		}
		else if (argument == "--frame-step" && index + 1 < argc)
		{
			const auto seconds = ParseSeconds(argv[++index]);
			if (!seconds)
			{
				PrintUsage(argv[0]);
				return std::nullopt;
			}
			arguments.frameStep = *seconds;
		}
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
	return arguments;
}


std::vector<std::string> TakeDueCommands(
    std::vector<RunCommand> &pending, std::uint64_t frame, double simulationSeconds)
{
	std::vector<std::string> due;
	std::erase_if(
	    pending,
	    [&](RunCommand &command)
	    {
		    const bool now = command.simulationSeconds ? simulationSeconds >= *command.simulationSeconds
		                                               : frame >= command.frame;
		    if (now)
			    due.push_back(std::move(command.line));
		    return now;
	    });
	return due;
}
} // namespace Jettison::App
