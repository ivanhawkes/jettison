#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <string>

namespace
{
void StubbedFunction()
{
	JETTISON_NOT_IMPLEMENTED();
}

// Runs check in a child process with its stderr captured, and reports whether the child was killed
// by SIGABRT and what it wrote. doctest has no death tests, and an abort is the behaviour under
// test. The child's stdout is discarded: doctest's crash handler reports the abort there.
struct ChildResult
{
	bool aborted;
	std::string stderrText;
};

template <typename Check> ChildResult RunInChild(Check check)
{
	std::array<int, 2> pipeEnds{};
	if (pipe(pipeEnds.data()) != 0)
		return {false, "pipe failed"};
	const pid_t child = fork();
	if (child == 0)
	{
		close(pipeEnds[0]);
		const int devNull = open("/dev/null", O_WRONLY);
		if (devNull < 0 || dup2(devNull, STDOUT_FILENO) < 0 || dup2(pipeEnds[1], STDERR_FILENO) < 0)
			_exit(2);
		check();
		_exit(0);
	}
	close(pipeEnds[1]);
	std::string text;
	std::array<char, 256> buffer{};
	for (ssize_t count = 0; (count = read(pipeEnds[0], buffer.data(), buffer.size())) > 0;)
		text.append(buffer.data(), static_cast<std::size_t>(count));
	close(pipeEnds[0]);
	int status = 0;
	waitpid(child, &status, 0);
	return {WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT, text};
}

template <typename Check> bool AbortsInChild(Check check)
{
	return RunInChild(check).aborted;
}
} // namespace

TEST_CASE("JETTISON_NOT_IMPLEMENTED throws, naming the function it is in")
{
	CHECK_THROWS_AS(StubbedFunction(), Jettison::Core::NotImplementedError);
	try
	{
		StubbedFunction();
	}
	catch (const Jettison::Core::NotImplementedError &error)
	{
		const std::string message = error.what();
		CHECK(message.find("StubbedFunction") != std::string::npos);
		CHECK(message.find("AssertTests.cpp") != std::string::npos);
	}
}

TEST_CASE("JETTISON_VERIFY passes a true condition and aborts on a false one")
{
	JETTISON_VERIFY(1 + 1 == 2);
	CHECK(AbortsInChild([] { JETTISON_VERIFY(1 + 1 == 3); }));
}

TEST_CASE("A failed check is reported through the log at Fatal, naming the expression")
{
	const ChildResult result = RunInChild([] { JETTISON_VERIFY(1 + 1 == 3); });
	CHECK(result.aborted);
	CAPTURE(result.stderrText);
	CHECK(
	    result.stderrText.find("[fatal] [assert] Assertion failed: 1 + 1 == 3, in ") !=
	    std::string::npos);
	CHECK(result.stderrText.find("AssertTests.cpp:") != std::string::npos);
}

TEST_CASE("JETTISON_ASSERT evaluates its condition in debug builds only")
{
	int evaluations = 0;
	const auto counted = [&evaluations]
	{
		++evaluations;
		return true;
	};
	JETTISON_ASSERT(counted());
#ifdef NDEBUG
	CHECK(evaluations == 0);
#else
	CHECK(evaluations == 1);
	CHECK(AbortsInChild([] { JETTISON_ASSERT(false); }));
#endif
}
