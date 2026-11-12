#include <jettison/app/AppArguments.h>

#include <doctest/doctest.h>

#include <string>
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
