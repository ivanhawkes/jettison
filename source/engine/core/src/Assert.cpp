#include <jettison/core/Assert.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace Jettison::Core
{
namespace
{
std::string Describe(const std::source_location &location)
{
	return std::string{location.function_name()} + " (" + location.file_name() + ":" +
	       std::to_string(location.line()) + ")";
}
} // namespace

NotImplementedError::NotImplementedError(const std::source_location &location)
    : std::logic_error{"Not implemented: " + Describe(location)}
{
}

void AssertionFailed(const char *expression, const std::source_location &location) noexcept
{
	// stdio rather than iostream: this runs on a path that may be failing because the process is
	// already in a bad state, and fprintf to an unbuffered stderr has the fewest moving parts.
	std::fprintf(
	    stderr, "Assertion failed: %s, in %s (%s:%u)\n", expression, location.function_name(),
	    location.file_name(), static_cast<unsigned>(location.line()));
	std::abort();
}

void NotImplemented(const std::source_location &location)
{
	throw NotImplementedError{location};
}
} // namespace Jettison::Core
