#pragma once

#include <filesystem>

#include <jettison/core/Log.h>
#include <jettison/platform/Paths.h>

namespace Jettison::App
{
// The run's log file in the state directory (Platform::Paths::state), which is the logs' and the crash
// reports' home. One fixed name, so a player finds it without being told a date, and it is truncated
// by each run (Core::InitLogging): a report is sent with the log of the run it is about, and a
// crash report's log tail covers the run that crashed.
inline constexpr const char *kRunLogFile = "jettison.log";

// What the file takes: Info and above, the same as the stderr default it joins, so a log line is
// in the file whenever a terminal would have shown it.
inline constexpr Core::Severity kRunLogMinimum = Core::Severity::Info;

inline std::filesystem::path RunLogPath(const Platform::Paths &paths)
{
	return paths.state / kRunLogFile;
}
} // namespace Jettison::App
