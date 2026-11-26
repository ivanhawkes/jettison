#include "../src/RunLog.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <unistd.h>

using namespace Jettison;


TEST_CASE("RunLog: the log is one file in the run's state directory, written by the run")
{
	const auto root =
	    std::filesystem::temp_directory_path() / ("jettison-runlog-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(root, error);
	const Platform::Paths paths{root / "data", root / "config", root / "state", root / "cache"};
	CHECK(App::RunLogPath(paths) == root / "state" / "jettison.log");

	// The directory does not exist yet: initialising creates it, and the file holds what was logged.
	constexpr Core::Category category{"runlog-test"};
	Core::InitLogging(App::kRunLogMinimum, App::RunLogPath(paths));
	Core::Log(Core::Severity::Info, category, "{}", "a line a player sends");
	Core::Log(Core::Severity::Debug, category, "{}", "a line below the minimum");
	Core::ShutdownLogging();

	std::ifstream in(App::RunLogPath(paths));
	const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
	CHECK(text.find("a line a player sends") != std::string::npos);
	CHECK(text.find("below the minimum") == std::string::npos);
	std::filesystem::remove_all(root, error);
}
