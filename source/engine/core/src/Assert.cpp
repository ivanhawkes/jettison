#include <jettison/core/Assert.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string>

#include <jettison/core/Log.h>

namespace Jettison::Core
{
namespace
{
constexpr Category kAssertLog{"assert"};

std::string Describe(const std::source_location &location)
{
	return std::string{location.function_name()} + " (" + location.file_name() + ":" +
	       std::to_string(location.line()) + ")";
}

// Linux reports the tracing process in /proc/self/status; 0 is none. Read on the failure path
// only, so its cost does not matter.
bool IsDebuggerAttached() noexcept
{
	try
	{
		std::ifstream status{"/proc/self/status"};
		std::string line;
		while (std::getline(status, line))
		{
			if (line.starts_with("TracerPid:"))
				return std::stoi(line.substr(10)) != 0;
		}
	}
	catch (...) // NOLINT(bugprone-empty-catch): no answer is the answer "no debugger".
	{
	}
	return false;
}
} // namespace

NotImplementedError::NotImplementedError(const std::source_location &location)
    : std::logic_error{"Not implemented: " + Describe(location)}
{
}

void AssertionFailed(const char *expression, const std::source_location &location) noexcept
{
	// Through the log, so the report reaches the file and the console as well as stderr, and is
	// flushed before the abort. The process may be failing because it is already in a bad state,
	// so if the log cannot take the report, stdio to an unbuffered stderr still can.
	try
	{
		LogMessage(
		    Severity::Fatal, kAssertLog,
		    std::format("Assertion failed: {}, in {}", expression, Describe(location)), location);
		FlushLogging();
	}
	catch (...)
	{
		std::fprintf(
		    stderr, "Assertion failed: %s, in %s (%s:%u)\n", expression, location.function_name(),
		    location.file_name(), static_cast<unsigned>(location.line()));
	}

	// A debugger stops here, at the failure, rather than in the abort handler's frames.
	if (IsDebuggerAttached())
		std::raise(SIGTRAP);
	std::abort();
}

void NotImplemented(const std::source_location &location)
{
	throw NotImplementedError{location};
}
} // namespace Jettison::Core
