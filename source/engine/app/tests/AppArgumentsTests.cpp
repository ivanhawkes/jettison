#include <jettison/app/AppArguments.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

using namespace Jettison::App;

namespace
{
std::optional<AppArguments> Parse(std::vector<std::string> words)
{
	words.insert(words.begin(), "/opt/jettison/render_test");
	std::vector<char *> argv;
	argv.reserve(words.size());
	for (std::string &word : words)
		argv.push_back(word.data());
	return ParseAppArguments(static_cast<int>(argv.size()), argv.data(), "assets");
}


// A line to run before the first frame (frame 0), on a frame, or at a simulation time.
RunCommand Line(std::string line, std::uint64_t frame = 0, std::optional<double> seconds = {})
{
	return {.line = std::move(line), .frame = frame, .simulationSeconds = seconds};
}
} // namespace


TEST_CASE("a screenshot is asked for with the frames before it, a fixed step and console lines")
{
	const auto arguments = Parse(
	    {"--screenshot", "/tmp/shots/orbit.png", "--after-frames", "12", "--frame-step", "0.02",
	     "--run", "orbit 0.5 -0.2 2000", "--run", "quit"});
	REQUIRE(arguments);
	CHECK(arguments->screenshotPath == "/tmp/shots/orbit.png");
	CHECK(arguments->screenshotAfterFrames == 12);
	CHECK(arguments->frameStep == doctest::Approx(0.02));
	CHECK(arguments->commands == std::vector<RunCommand>{Line("orbit 0.5 -0.2 2000"), Line("quit")});

	const auto defaults = Parse({});
	REQUIRE(defaults);
	CHECK(defaults->screenshotPath.empty());
	CHECK(defaults->frameStep == 0.0);
	CHECK(defaults->commands.empty());
}


TEST_CASE("a console line runs before the first frame, on a frame, or at a simulation time")
{
	const auto arguments = Parse(
	    {"--offscreen", "--run", "pilot chase", "--run", "@40 undock", "--run",
	     "@2.5s screenshot out.png"});
	REQUIRE(arguments);
	CHECK(arguments->offscreen);
	CHECK(
	    arguments->commands ==
	    std::vector<RunCommand>{
	        Line("pilot chase"), Line("undock", 40), Line("screenshot out.png", 0, 2.5)});
	CHECK_FALSE(Parse({})->offscreen);

	CHECK_FALSE(Parse({"--run", "@40"}));
	CHECK_FALSE(Parse({"--run", "@40 "}));
	CHECK_FALSE(Parse({"--run", "@0 undock"}));
	CHECK_FALSE(Parse({"--run", "@-1s undock"}));
	CHECK_FALSE(Parse({"--run", "@soon undock"}));
	CHECK_FALSE(Parse({"--run", "@ undock"}));
}


TEST_CASE("a console line is taken once, when its frame or its simulation time comes, in order")
{
	std::vector<RunCommand> pending{
	    Line("at once"), Line("frame 3", 3), Line("half a second", 0, 0.5), Line("also frame 3", 3)};
	CHECK(TakeDueCommands(pending, 1, 0.0) == std::vector<std::string>{"at once"});
	CHECK(TakeDueCommands(pending, 2, 0.4).empty());
	CHECK(TakeDueCommands(pending, 3, 0.4) == std::vector<std::string>{"frame 3", "also frame 3"});
	CHECK(TakeDueCommands(pending, 4, 0.5) == std::vector<std::string>{"half a second"});
	CHECK(pending.empty());
	CHECK(TakeDueCommands(pending, 5, 1.0).empty());
}


TEST_CASE("a frame count or a step that is not a positive number is not understood")
{
	CHECK_FALSE(Parse({"--after-frames", "0"}));
	CHECK_FALSE(Parse({"--after-frames", "ten"}));
	CHECK_FALSE(Parse({"--frame-step", "0"}));
	CHECK_FALSE(Parse({"--frame-step", "-0.1"}));
	CHECK_FALSE(Parse({"--frame-step", "0.1s"}));
	CHECK_FALSE(Parse({"--screenshot"}));
}


TEST_CASE("the assets are the staged directory, or its pack where there is none")
{
	const std::filesystem::path root = std::filesystem::temp_directory_path() /
	                                   ("jettison-arguments-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(root, error);
	std::filesystem::create_directories(root);

	// An absolute subdirectory is where it says, wherever the executable is.
	const auto parse = [&root]
	{
		std::string executable = "render_test";
		char *argv[] = {executable.data()};
		return ParseAppArguments(1, argv, root / "assets");
	};

	CHECK(parse()->assets == root / "assets");
	std::ofstream{root / "assets.pack"} << "a pack";
	CHECK(parse()->assets == root / "assets.pack");
	std::filesystem::create_directories(root / "assets");
	CHECK(parse()->assets == root / "assets");

	// No scene named is the assets' own, read through the mounts.
	CHECK(parse()->scenePath.empty());
	CHECK(Parse({"--assets", "/opt/jettison/base.pack"})->assets == "/opt/jettison/base.pack");
	std::filesystem::remove_all(root, error);
}
