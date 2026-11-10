#pragma once

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Jettison::Core
{
// Logging: a severity, a category and a message, to sinks (stderr, a file, and callbacks such as
// the console's). Engine code logs through here and never through spdlog directly, so categories
// and sinks stay the engine's (docs/history.md, entry 3). spdlog writes the stderr and file sinks;
// this file owns which messages reach them.
//
// Before InitLogging is called, and after ShutdownLogging, messages at Info and above go to
// stderr: a log line that goes nowhere is a lost report. Every function here is thread-safe.
enum class Severity : std::uint8_t
{
	Trace,
	Debug,
	Info,
	Warning,
	Error,
	// The report a failed assertion makes before it aborts. Logging at Fatal does not abort.
	Fatal
};

// A category is a name a sink can filter on: "render", "sim", "vfs". The name is not copied, so
// it has to outlive the call: a literal or a constant.
struct Category
{
	std::string_view name;
};

// One message, as a callback sees it. The views are valid for the duration of the call only.
struct LogRecord
{
	Severity severity;
	std::string_view category;
	std::string_view message;
	std::source_location location;
};

// A format string checked at compile time, with the caller's location captured alongside it. It
// lets Log take a variadic argument list and still default the location to the call site.
template <typename... Args> struct LogFormat
{
	template <typename Text>
	    requires std::convertible_to<const Text &, std::string_view>
	// Implicit, so a string literal is the argument.
	consteval LogFormat(const Text &text, std::source_location where = std::source_location::current())
	    : format{text}, location{where}
	{
	}

	std::format_string<Args...> format;
	std::source_location location;
};

// Whether a message at this severity would reach any sink; Log checks it before formatting.
bool IsLogEnabled(Severity severity) noexcept;

// Delivers an already formatted message. Log formats and calls this; the assertion report calls
// it directly.
void LogMessage(
    Severity severity, const Category &category, std::string_view message,
    const std::source_location &location);

// Log(Severity::Info, kRender, "swapchain {}x{}", width, height). The arguments are formatted
// only when the severity is enabled. A runtime string is logged as Log(severity, category, "{}",
// text): the format must be a constant, which is what lets std::format check it.
template <typename... Args>
void Log(
    Severity severity, const Category &category, LogFormat<std::type_identity_t<Args>...> format,
    Args &&...args)
{
	if (!IsLogEnabled(severity))
		return;
	LogMessage(
	    severity, category, std::format(format.format, std::forward<Args>(args)...), format.location);
}

// Installs the sinks: stderr always, and the file when one is given, truncated, its directory
// created. Messages below minimum are dropped. Called once by the app shell, and again to change
// the sinks. Throws when the file cannot be opened.
void InitLogging(Severity minimum, const std::filesystem::path &file = {});

// Flushes and closes the file sink, and returns to the stderr default above. Callbacks stay
// registered: their owners remove them.
void ShutdownLogging() noexcept;

// Writes out whatever the sinks have buffered. Messages at Error and above are flushed as they
// are written.
void FlushLogging() noexcept;

// A sink the engine provides itself, such as the console's log view. It is called for every
// message at or above the minimum, on the thread that logged it, with the logging lock held: it
// must not log, and should return quickly.
using LogCallback = std::function<void(const LogRecord &)>;
using LogCallbackId = std::uint32_t;

LogCallbackId AddLogCallback(LogCallback callback);
void RemoveLogCallback(LogCallbackId id);
} // namespace Jettison::Core
