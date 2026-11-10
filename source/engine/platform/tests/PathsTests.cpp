#include <jettison/platform/Paths.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

using namespace Jettison::Platform;

namespace
{
EnvironmentLookup Environment(std::map<std::string, std::string> variables)
{
	return [variables = std::move(variables)](const char *name) -> std::optional<std::string>
	{
		const auto found = variables.find(name);
		if (found == variables.end())
			return std::nullopt;
		return found->second;
	};
}
} // namespace

TEST_CASE("Paths: with only HOME set, each directory is the specification's default under it")
{
	const Paths paths = ResolvePaths("jettison", Environment({{"HOME", "/home/pilot"}}));
	CHECK(paths.userData == "/home/pilot/.local/share/jettison");
	CHECK(paths.config == "/home/pilot/.config/jettison");
	CHECK(paths.state == "/home/pilot/.local/state/jettison");
	CHECK(paths.cache == "/home/pilot/.cache/jettison");
}

TEST_CASE("Paths: an XDG variable overrides its default")
{
	const Paths paths = ResolvePaths(
	    "jettison", Environment(
	                    {{"HOME", "/home/pilot"},
	                     {"XDG_DATA_HOME", "/data"},
	                     {"XDG_CONFIG_HOME", "/etc/pilot"},
	                     {"XDG_STATE_HOME", "/var/pilot"},
	                     {"XDG_CACHE_HOME", "/tmp/cache"}}));
	CHECK(paths.userData == "/data/jettison");
	CHECK(paths.config == "/etc/pilot/jettison");
	CHECK(paths.state == "/var/pilot/jettison");
	CHECK(paths.cache == "/tmp/cache/jettison");
}

TEST_CASE("Paths: an XDG variable that is empty or relative is ignored, as the specification says")
{
	const Paths paths = ResolvePaths(
	    "jettison",
	    Environment({{"HOME", "/home/pilot"}, {"XDG_DATA_HOME", ""}, {"XDG_CACHE_HOME", "cache"}}));
	CHECK(paths.userData == "/home/pilot/.local/share/jettison");
	CHECK(paths.cache == "/home/pilot/.cache/jettison");
}

TEST_CASE("Paths: HOME is needed only for a directory with no XDG variable")
{
	CHECK_THROWS_AS(ResolvePaths("jettison", Environment({})), std::runtime_error);
	CHECK_THROWS_AS(ResolvePaths("jettison", Environment({{"HOME", "relative"}})), std::runtime_error);
	const Paths paths = ResolvePaths(
	    "jettison", Environment(
	                    {{"XDG_DATA_HOME", "/d"},
	                     {"XDG_CONFIG_HOME", "/c"},
	                     {"XDG_STATE_HOME", "/s"},
	                     {"XDG_CACHE_HOME", "/k"}}));
	CHECK(paths.config == "/c/jettison");
}

TEST_CASE("Paths: the application name is one relative path component")
{
	const auto environment = Environment({{"HOME", "/home/pilot"}});
	CHECK_THROWS_AS(ResolvePaths("", environment), std::invalid_argument);
	CHECK_THROWS_AS(ResolvePaths("/jettison", environment), std::invalid_argument);
	CHECK_THROWS_AS(ResolvePaths("games/jettison", environment), std::invalid_argument);
	CHECK_THROWS_AS(ResolvePaths("..", environment), std::invalid_argument);
	CHECK_THROWS_AS(ResolvePaths(".", environment), std::invalid_argument);
}

TEST_CASE("Paths: the process environment resolves to absolute paths under the application name")
{
	const Paths paths = ResolvePaths("jettison");
	for (const std::filesystem::path &path : {paths.userData, paths.config, paths.state, paths.cache})
	{
		CAPTURE(path);
		CHECK(path.is_absolute());
		CHECK(path.filename() == "jettison");
	}
}

TEST_CASE("Paths: the executable path is this test executable")
{
	const std::filesystem::path executable = ExecutablePath();
	CHECK(executable.is_absolute());
	CHECK(executable.filename() == "jettison_platform_tests");
	CHECK(std::filesystem::exists(executable));
}
