#include <jettison/platform/Cpu.h>
#include <jettison/platform/Paths.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

using Jettison::Core::NotImplementedError;

TEST_CASE("Platform: the stubs fail loudly until phase 2")
{
	CHECK_THROWS_AS(Jettison::Platform::ResolvePaths("jettison"), NotImplementedError);
	CHECK_THROWS_AS(Jettison::Platform::ExecutablePath(), NotImplementedError);
	CHECK_THROWS_AS(Jettison::Platform::CheckCpuBaseline(), NotImplementedError);
}
