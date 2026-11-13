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
	CHECK(arguments->commands == std::vector<std::string>{"orbit 0.5 -0.2 2000", "quit"});

	const auto defaults = Parse({});
	REQUIRE(defaults);
	CHECK(defaults->screenshotPath.empty());
	CHECK(defaults->frameStep == 0.0);
	CHECK(defaults->commands.empty());
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
