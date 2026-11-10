#include <jettison/ai/Pathfinding.h>
#include <jettison/ai/Steering.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Ai: the stubs fail loudly until phase 9")
{
	const Jettison::Ai::Graph graph{{{{1, 1.0}}, {}}};
	CHECK_THROWS_AS(Jettison::Ai::FindPath(graph, 0, 1), Jettison::Core::NotImplementedError);
	const Jettison::Ai::Agent agent;
	CHECK_THROWS_AS(Jettison::Ai::Seek(agent, {1.0, 0.0, 0.0}), Jettison::Core::NotImplementedError);
}
