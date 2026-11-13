#include <jettison/core/Log.h>

#include <spdlog/details/log_msg.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
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

constexpr std::string_view kFilePattern = "[%Y-%m-%d %H:%M:%S.%e] [%*] [%n] %v (%s:%#)";

std::shared_ptr<spdlog::sinks::sink> MakeFileSink(const std::filesystem::path &file)
{
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file.string(), true);
	sink->set_formatter(MakeFormatter(std::string{kFilePattern}));
	return sink;
}

// The log's tail, a ring of bytes written under the log's lock and read by a crash handler without
// it. The count of bytes ever written is published after the bytes, so a reader that loads it sees
// them; a writer racing the reader can only overwrite the oldest, which the reader skips to a line
// past.
class Tail
{
  public:
	void Append(std::string_view text) noexcept
	{
		std::uint64_t written = m_written.load(std::memory_order_relaxed);
		for (const char byte : text)
			m_bytes[written++ % kLogTailBytes] = byte;
		m_written.store(written, std::memory_order_release);
	}

	// The two runs of the ring, oldest first, from the first line that is whole in it.
	template <typename Write> void Read(Write write) const noexcept
	{
		const std::uint64_t written = m_written.load(std::memory_order_acquire);
		std::uint64_t begin = written > kLogTailBytes ? written - kLogTailBytes : 0;
		if (begin != 0)
		{
			while (begin < written && m_bytes[begin % kLogTailBytes] != '\n')
				++begin;
			begin += begin < written ? 1 : 0;
		}
		const std::size_t start = begin % kLogTailBytes;
		const std::size_t count = written - begin;
		const std::size_t first = count < kLogTailBytes - start ? count : kLogTailBytes - start;
		write(m_bytes.data() + start, first);
		write(m_bytes.data(), count - first);
	}

  private:
	std::array<char, kLogTailBytes> m_bytes{};
	std::atomic<std::uint64_t> m_written{0};
};

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
	std::unique_ptr<spdlog::formatter> tailFormatter{MakeFormatter(std::string{kFilePattern})};
	Tail tail;
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
	spdlog::memory_buf_t line;
	state.tailFormatter->format(record, line);
	state.tail.Append({line.data(), line.size()});
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

std::string LogTail()
{
	std::string text;
	GetState().tail.Read([&text](const char *bytes, std::size_t count) { text.append(bytes, count); });
	return text;
}

void WriteLogTail(int fileDescriptor) noexcept
{
	GetState().tail.Read(
	    [fileDescriptor](const char *bytes, std::size_t count)
	    {
		    while (count > 0)
		    {
			    const ssize_t wrote = ::write(fileDescriptor, bytes, count);
			    if (wrote < 0 && errno == EINTR)
				    continue;
			    if (wrote <= 0)
				    return;
			    bytes += wrote;
			    count -= static_cast<std::size_t>(wrote);
		    }
	    });
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
