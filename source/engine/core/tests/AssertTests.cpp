#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <string>

namespace
{
void StubbedFunction()
{
	JETTISON_NOT_IMPLEMENTED();
}

// Runs check in a child process with stderr discarded and reports whether the child was killed by
// SIGABRT. doctest has no death tests, and an abort is the behaviour under test.
template <typename Check> bool AbortsInChild(Check check)
{
	const pid_t child = fork();
	if (child == 0)
	{
		const int devNull = open("/dev/null", O_WRONLY);
		if (devNull < 0 || dup2(devNull, STDERR_FILENO) < 0)
			_exit(2);
		check();
		_exit(0);
	}
	int status = 0;
	waitpid(child, &status, 0);
	return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
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
