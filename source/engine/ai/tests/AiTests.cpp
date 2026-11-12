#include <jettison/ai/UtilityAi.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Ai: utility AI's stubs fail loudly until phase 9f")
{
	entt::registry registry;
	CHECK_THROWS_AS(
	    (void)Jettison::Ai::Choose({}, registry, registry.create()),
	    Jettison::Core::NotImplementedError);
}
