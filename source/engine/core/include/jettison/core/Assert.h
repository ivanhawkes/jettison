#pragma once

#include <source_location>
#include <stdexcept>

// Phase 1 gives these their final meaning and the smallest implementation that keeps it: a failed
// check names the expression and where it is, then aborts. Phase 2 routes the report through
// Log.h and breaks into an attached debugger first; the macros and what they promise stay.
//
// JETTISON_VERIFY checks in every build. JETTISON_ASSERT checks in debug builds and does not
// evaluate its condition in release, so neither may have side effects in its condition.
// JETTISON_NOT_IMPLEMENTED marks a stub that has been asked to do its job before the phase that
// fills it in: it throws rather than aborts, because calling a stub is how a test proves the stub
// fails loudly, and because the app shell already reports an exception with its teardown done.
#define JETTISON_VERIFY(condition)                                                                     \
	((condition) ? static_cast<void>(0)                                                                \
	             : ::Jettison::Core::AssertionFailed(#condition, std::source_location::current()))

#ifdef NDEBUG
// sizeof does not evaluate its operand, and still counts as a use of every name in it, so a
// variable that only an assertion reads does not become an unused-variable warning in release.
#define JETTISON_ASSERT(condition) static_cast<void>(sizeof(static_cast<bool>(condition)))
#else
#define JETTISON_ASSERT(condition) JETTISON_VERIFY(condition)
#endif

#define JETTISON_NOT_IMPLEMENTED() ::Jettison::Core::NotImplemented(std::source_location::current())

namespace Jettison::Core
{
// What JETTISON_NOT_IMPLEMENTED throws. A logic_error, because reaching a stub is a defect in the
// caller's expectations, not a condition the caller can recover from at run time.
class NotImplementedError : public std::logic_error
{
  public:
	explicit NotImplementedError(const std::source_location &location);
};

// Reports a failed JETTISON_VERIFY or JETTISON_ASSERT on stderr and aborts. Called through the
// macros, which supply the expression's text and its location.
[[noreturn]] void AssertionFailed(
    const char *expression, const std::source_location &location) noexcept;

// Throws NotImplementedError naming the function that location is in.
[[noreturn]] void NotImplemented(const std::source_location &location);
} // namespace Jettison::Core
