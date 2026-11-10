#include <jettison/core/Log.h>

#include <spdlog/details/log_msg.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <ctime>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Jettison::Core
{
namespace
{
constexpr Severity kDefaultMinimum = Severity::Info;

spdlog::level::level_enum ToSpdlog(Severity severity)
{
	switch (severity)
	{
		case Severity::Trace: return spdlog::level::trace;
		case Severity::Debug: return spdlog::level::debug;
		case Severity::Info: return spdlog::level::info;
		case Severity::Warning: return spdlog::level::warn;
		case Severity::Error: return spdlog::level::err;
		case Severity::Fatal: return spdlog::level::critical;
	}
	return spdlog::level::critical;
}

std::string_view SeverityName(spdlog::level::level_enum level)
{
	switch (level)
	{
		case spdlog::level::trace: return "trace";
		case spdlog::level::debug: return "debug";
		case spdlog::level::info: return "info";
		case spdlog::level::warn: return "warning";
		case spdlog::level::err: return "error";
		case spdlog::level::critical: return "fatal";
		case spdlog::level::off:
		case spdlog::level::n_levels: break;
	}
	return "unknown";
}

// %* in a pattern: the engine's name for the severity. spdlog's own %l says "critical" for what
// the engine calls Fatal, and a report should use the word the code uses.
class SeverityFlag final : public spdlog::custom_flag_formatter
{
  public:
	void format(
	    const spdlog::details::log_msg &message, const std::tm & /*unused*/,
	    spdlog::memory_buf_t &dest) override
	{
		const std::string_view name = SeverityName(message.level);
		dest.append(name.data(), name.data() + name.size());
	}
	std::unique_ptr<custom_flag_formatter> clone() const override
	{
		return std::make_unique<SeverityFlag>();
	}
};

std::unique_ptr<spdlog::formatter> MakeFormatter(const std::string &pattern)
{
	auto formatter = std::make_unique<spdlog::pattern_formatter>();
	formatter->add_flag<SeverityFlag>('*').set_pattern(pattern);
	return formatter;
}

// stderr is for a person watching: the time of day, the severity in colour, the category. The file
// is for reading afterwards, so it carries the date and the source location as well.
std::shared_ptr<spdlog::sinks::sink> MakeStderrSink()
{
	auto sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
	sink->set_formatter(MakeFormatter("[%H:%M:%S.%e] [%^%*%$] [%n] %v"));
	return sink;
}

std::shared_ptr<spdlog::sinks::sink> MakeFileSink(const std::filesystem::path &file)
{
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string(), true);
	sink->set_formatter(MakeFormatter("[%Y-%m-%d %H:%M:%S.%e] [%*] [%n] %v (%s:%#)"));
	return sink;
}

struct Callback
{
	LogCallbackId id;
	LogCallback function;
};

// The sinks and callbacks, behind one lock. Messages are dispatched to spdlog's sinks directly
// rather than through an spdlog::logger, because the category is per message: a logger has one
// name, and a logger per category would be a registry lookup on every call.
struct State
{
	std::mutex mutex;
	std::atomic<Severity> minimum{kDefaultMinimum};
	std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks{MakeStderrSink()};
	std::vector<Callback> callbacks;
	LogCallbackId nextCallbackId{1};
};

State &GetState()
{
	static State state;
	return state;
}
} // namespace

bool IsLogEnabled(Severity severity) noexcept
{
	return severity >= GetState().minimum.load(std::memory_order_relaxed);
}

void LogMessage(
    Severity severity, const Category &category, std::string_view message,
    const std::source_location &location)
{
	State &state = GetState();
	if (severity < state.minimum.load(std::memory_order_relaxed))
		return;

	const spdlog::details::log_msg record{
	    spdlog::source_loc{
	        location.file_name(), static_cast<int>(location.line()), location.function_name()},
	    category.name, ToSpdlog(severity), message};

	const std::scoped_lock lock{state.mutex};
	for (const auto &sink : state.sinks)
	{
		sink->log(record);
		if (severity >= Severity::Error)
			sink->flush();
	}
	for (const auto &callback : state.callbacks)
		callback.function(LogRecord{severity, category.name, message, location});
}

void InitLogging(Severity minimum, const std::filesystem::path &file)
{
	// Built before the lock is taken, so a file that cannot be opened throws with the old sinks
	// still in place and still reporting.
	std::vector<std::shared_ptr<spdlog::sinks::sink>> sinks{MakeStderrSink()};
	if (!file.empty())
		sinks.push_back(MakeFileSink(file));

	State &state = GetState();
	const std::scoped_lock lock{state.mutex};
	for (const auto &sink : state.sinks)
		sink->flush();
	state.sinks = std::move(sinks);
	state.minimum.store(minimum, std::memory_order_relaxed);
}

void ShutdownLogging() noexcept
{
	State &state = GetState();
	const std::scoped_lock lock{state.mutex};
	try
	{
		for (const auto &sink : state.sinks)
			sink->flush();
		state.sinks.resize(1); // The stderr sink is always first.
	}
	catch (...) // NOLINT(bugprone-empty-catch): teardown has nowhere left to report to.
	{
	}
	state.minimum.store(kDefaultMinimum, std::memory_order_relaxed);
}

void FlushLogging() noexcept
{
	State &state = GetState();
	const std::scoped_lock lock{state.mutex};
	try
	{
		for (const auto &sink : state.sinks)
			sink->flush();
	}
	catch (...) // NOLINT(bugprone-empty-catch): a flush that fails has nowhere to report to.
	{
	}
}

LogCallbackId AddLogCallback(LogCallback callback)
{
	State &state = GetState();
	const std::scoped_lock lock{state.mutex};
	const LogCallbackId id = state.nextCallbackId++;
	state.callbacks.push_back(Callback{id, std::move(callback)});
	return id;
}

void RemoveLogCallback(LogCallbackId id)
{
	State &state = GetState();
	const std::scoped_lock lock{state.mutex};
	std::erase_if(state.callbacks, [id](const Callback &callback) { return callback.id == id; });
}
} // namespace Jettison::Core
