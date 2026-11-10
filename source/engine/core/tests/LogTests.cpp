#include <jettison/core/Log.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Jettison::Core;

namespace
{
constexpr Category kTestLog{"test"};

// Counts the times it is formatted, to show a dropped message is never formatted.
struct CountsFormats
{
	int *count;
};
} // namespace

template <> struct std::formatter<CountsFormats> : std::formatter<int>
{
	auto format(const CountsFormats &value, std::format_context &context) const
	{
		return std::formatter<int>::format(++*value.count, context);
	}
};

namespace
{

struct Captured
{
	Severity severity;
	std::string category;
	std::string message;
	std::string function;
	unsigned line;
};

// Registers a callback for the life of the object and keeps what it was given.
class Capture
{
  public:
	Capture()
	    : m_id{AddLogCallback(
	          [this](const LogRecord &record)
	          {
		          records.push_back(
		              Captured{
		                  record.severity, std::string{record.category}, std::string{record.message},
		                  record.location.function_name(),
		                  static_cast<unsigned>(record.location.line())});
	          })}
	{
	}
	~Capture()
	{
		RemoveLogCallback(m_id);
	}
	Capture(const Capture &) = delete;
	Capture &operator=(const Capture &) = delete;

	std::vector<Captured> records;

  private:
	LogCallbackId m_id;
};

std::filesystem::path TemporaryPath(const std::string &name)
{
	return std::filesystem::temp_directory_path() /
	       ("jettison-core-tests-" + std::to_string(getpid())) / name;
}

std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file{path};
	return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
} // namespace

TEST_CASE("Log: formats its arguments and reports the caller's location")
{
	InitLogging(Severity::Trace);
	const Capture capture;
	const unsigned line = __LINE__ + 1;
	Log(Severity::Info, kTestLog, "swapchain {}x{}", 640, 480);

	REQUIRE(capture.records.size() == 1);
	const Captured &record = capture.records.front();
	CHECK(record.severity == Severity::Info);
	CHECK(record.category == "test");
	CHECK(record.message == "swapchain 640x480");
	CHECK(record.line == line);
	CHECK(record.function.find("DOCTEST_ANON_FUNC") != std::string::npos);
	ShutdownLogging();
}

TEST_CASE("Log: messages below the minimum are dropped before they are formatted")
{
	InitLogging(Severity::Warning);
	const Capture capture;
	CHECK_FALSE(IsLogEnabled(Severity::Info));
	CHECK(IsLogEnabled(Severity::Error));

	int formatted = 0;
	Log(Severity::Info, kTestLog, "{}", CountsFormats{&formatted});
	CHECK(capture.records.empty());
	CHECK(formatted == 0);

	Log(Severity::Warning, kTestLog, "kept {}", CountsFormats{&formatted});
	Log(Severity::Fatal, kTestLog, "kept too");
	CHECK(capture.records.size() == 2);
	CHECK(formatted == 1);
	ShutdownLogging();
}

TEST_CASE("Log: the defaults before InitLogging and after ShutdownLogging are Info and above")
{
	ShutdownLogging();
	CHECK_FALSE(IsLogEnabled(Severity::Debug));
	CHECK(IsLogEnabled(Severity::Info));
	ShutdownLogging(); // Twice is safe.
}

TEST_CASE("Log: a removed callback hears nothing more")
{
	InitLogging(Severity::Trace);
	std::vector<std::string> heard;
	const auto id =
	    AddLogCallback([&heard](const LogRecord &record) { heard.emplace_back(record.message); });
	Log(Severity::Debug, kTestLog, "one");
	RemoveLogCallback(id);
	Log(Severity::Debug, kTestLog, "two");
	CHECK(heard == std::vector<std::string>{"one"});
	ShutdownLogging();
}

TEST_CASE("Log: the file sink writes the date, severity, category and source location")
{
	const auto path = TemporaryPath("log/engine.log");
	std::filesystem::remove_all(path.parent_path());
	InitLogging(Severity::Debug, path);
	Log(Severity::Warning, kTestLog, "dock {} refused", 7);
	Log(Severity::Fatal, kTestLog, "the word the engine uses");
	ShutdownLogging();

	const std::string contents = ReadFile(path);
	CHECK(contents.find("[warning] [test] dock 7 refused (LogTests.cpp:") != std::string::npos);
	CHECK(contents.find("[fatal] [test] the word the engine uses") != std::string::npos);
	CHECK(contents.starts_with("[20"));
	std::filesystem::remove_all(path.parent_path().parent_path());
}

TEST_CASE("Log: a file that cannot be opened throws, and the sinks before it keep working")
{
	InitLogging(Severity::Trace);
	const Capture capture;
	CHECK_THROWS(InitLogging(Severity::Trace, "/proc/jettison/cannot/exist.log"));
	Log(Severity::Debug, kTestLog, "still heard");
	CHECK(capture.records.size() == 1);
	ShutdownLogging();
}
