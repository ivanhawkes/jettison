#include <jettison/platform/Crash.h>

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <jettison/core/Assert.h>
#include <jettison/core/Log.h>

namespace
{
constexpr Jettison::Core::Category kTestLog{"test"};
// What a child exits with when the build's sanitiser keeps the signals and nothing was installed.
constexpr int kNotInstalled = 3;

struct Crashed
{
	int signal{0};
	int exitStatus{-1};
	std::string stderrText;
	std::string report;
	std::size_t reports{0};
};

std::filesystem::path TemporaryDirectory(const std::string &name)
{
	return std::filesystem::temp_directory_path() /
	       ("jettison-platform-tests-" + std::to_string(getpid())) / name;
}

std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file{path};
	return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

// Installs the handler in a child, logs a line and runs crash, then reads what the child wrote. The
// child's stdout is discarded: doctest's own reports are not the handler's.
template <typename Crash> Crashed CrashInChild(const std::string &name, Crash crash)
{
	const std::filesystem::path directory = TemporaryDirectory(name);
	std::filesystem::remove_all(directory);
	std::array<int, 2> pipeEnds{};
	REQUIRE(pipe(pipeEnds.data()) == 0);
	const pid_t child = fork();
	if (child == 0)
	{
		close(pipeEnds[0]);
		const int devNull = open("/dev/null", O_WRONLY);
		if (devNull < 0 || dup2(devNull, STDOUT_FILENO) < 0 || dup2(pipeEnds[1], STDERR_FILENO) < 0)
			_exit(2);
		if (!Jettison::Platform::InstallCrashHandler({directory, "the platform suite 1.2.3", {}, true}))
			_exit(kNotInstalled);
		Jettison::Core::Log(Jettison::Core::Severity::Info, kTestLog, "the line before the {}", name);
		crash();
		_exit(0);
	}
	close(pipeEnds[1]);
	Crashed crashed;
	std::array<char, 4096> buffer{};
	for (ssize_t count = 0; (count = read(pipeEnds[0], buffer.data(), buffer.size())) > 0;)
		crashed.stderrText.append(buffer.data(), static_cast<std::size_t>(count));
	close(pipeEnds[0]);
	int status = 0;
	waitpid(child, &status, 0);
	crashed.signal = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
	crashed.exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	std::error_code error;
	for (const auto &entry : std::filesystem::directory_iterator{directory, error})
	{
		++crashed.reports;
		crashed.report = ReadFile(entry.path());
		CHECK(entry.path().filename().string().starts_with("crash-20"));
		CHECK(entry.path().filename().string().ends_with("-" + std::to_string(child) + ".txt"));
	}
	std::filesystem::remove_all(directory);
	return crashed;
}

bool Has(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

bool Symbolised()
{
	return std::system("command -v addr2line >/dev/null 2>&1") == 0;
}

// Not inlined, so the report has a frame of its own to name.
[[gnu::noinline]] void WriteThroughNull()
{
	volatile int *volatile pTarget = nullptr;
	*pTarget = 1;
}
} // namespace

TEST_CASE("Crash: a fault writes a report of the signal, the frames and the log's tail")
{
	const Crashed crashed = CrashInChild("fault", [] { WriteThroughNull(); });
	if (crashed.exitStatus == kNotInstalled)
	{
		MESSAGE("The address sanitiser keeps the fatal signals in this build");
		return;
	}
	CHECK(crashed.signal == SIGSEGV);
	REQUIRE(crashed.reports == 1);
	const std::string &report = crashed.report;
	INFO("The report:\n", report, "\nstderr:\n", crashed.stderrText);
	CHECK(Has(report, "Fatal signal SIGSEGV (11), an address that is not mapped, at 0x0\n"));
	CHECK(Has(report, ", the main thread, of process "));
	CHECK(Has(report, "\nBuild the platform suite 1.2.3\n"));
	CHECK(Has(report, "\nCommand "));
	CHECK(Has(report, "jettison_platform_tests"));
	// Frame 0 is the instruction that faulted, in this executable.
	CHECK(Has(report, "\n  #00 0x"));
	CHECK(Has(report, "/jettison_platform_tests+0x"));
	if (Symbolised())
	{
		CHECK(Has(report, "Frames, named by "));
		CHECK(Has(report, "WriteThroughNull() at "));
		CHECK(Has(report, "CrashTests.cpp:"));
	}
	CHECK(Has(report, "The log's tail:\n["));
	CHECK(Has(report, "[info] [test] the line before the fault (CrashTests.cpp:"));
	// stderr has the head and the frames, and where the report is, not the tail.
	CHECK(Has(crashed.stderrText, "Fatal signal SIGSEGV (11)"));
	CHECK(Has(crashed.stderrText, "/jettison_platform_tests+0x"));
	CHECK(Has(crashed.stderrText, "The crash report is "));
	CHECK(!Has(crashed.stderrText, "The log's tail"));
	CHECK(!Has(crashed.stderrText, "the line before the fault (CrashTests.cpp:"));
}

TEST_CASE("Crash: a failed assertion's abort is reported, with the assertion in the log's tail")
{
	const Crashed crashed = CrashInChild("assertion", [] { JETTISON_VERIFY(1 + 1 == 3); });
	if (crashed.exitStatus == kNotInstalled)
		return;
	CHECK(crashed.signal == SIGABRT);
	REQUIRE(crashed.reports == 1);
	CHECK(Has(crashed.report, "Fatal signal SIGABRT (6), sent by a process\n"));
	CHECK(!Has(crashed.report, ", at 0x"));
	CHECK(Has(crashed.report, "[fatal] [assert] Assertion failed: 1 + 1 == 3"));
	if (Symbolised())
		CHECK(Has(crashed.report, "Jettison::Core::AssertionFailed("));
}

TEST_CASE("Crash: an exception that goes uncaught is logged, then reported as an abort")
{
	const Crashed crashed = CrashInChild(
	    "exception",
	    []
	    {
		    // Thrown on a thread, where nothing catches it.
		    std::thread{[] { throw std::runtime_error{"the drive is not where it was"}; }}.join();
	    });
	if (crashed.exitStatus == kNotInstalled)
		return;
	CHECK(crashed.signal == SIGABRT);
	REQUIRE(crashed.reports == 1);
	CHECK(Has(
	    crashed.report, "[fatal] [crash] An exception went uncaught: the drive is not where it was"));
}

TEST_CASE("Crash: a directory that cannot be made is refused as the handler is installed")
{
	CHECK_THROWS_AS(
	    Jettison::Platform::InstallCrashHandler({"/proc/jettison/crashes", "", {}, false}),
	    std::runtime_error);
}
