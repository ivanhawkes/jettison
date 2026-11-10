#pragma once

#include <cstdint>
#include <source_location>
#include <string_view>

namespace Jettison::Core
{
// Logging: a severity, a category and a message, to sinks (stderr, a file, the console). Engine
// code logs through here and never through spdlog directly, so categories and sinks stay the
// engine's (docs/roadmap.md, entry 3). Phase 2 fills this in over spdlog with std::format;
// until then every call fails loudly, because a log line that goes nowhere is a lost report.
enum class Severity : std::uint8_t
{
	Trace,
	Debug,
	Info,
	Warning,
	Error,
	Fatal
};

// A category is a name a sink can filter on: "render", "sim", "vfs".
struct Category
{
	std::string_view name;
};

void Log(
    Severity severity, const Category &category, std::string_view message,
    const std::source_location &location = std::source_location::current());

// Installs the sinks (stderr always; a file under the platform's user data directory when one
// is given) and the minimum severity each passes. Called once by the app shell.
void InitLogging(Severity minimum);
void ShutdownLogging() noexcept;
} // namespace Jettison::Core
