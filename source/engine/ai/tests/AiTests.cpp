#include <jettison/ai/Steering.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Ai: steering's stubs fail loudly until phase 9b")
{
	const Jettison::Ai::Agent agent;
	CHECK_THROWS_AS(Jettison::Ai::Seek(agent, {1.0, 0.0, 0.0}), Jettison::Core::NotImplementedError);
}
