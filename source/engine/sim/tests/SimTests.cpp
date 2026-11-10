#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/SimWorld.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

#include <memory>

using namespace Jettison::Sim;

namespace
{
struct SetThrottle : Command
{
	float throttle{0.0f};
};
} // namespace

TEST_CASE("Sim: commands drain in the order they were pushed, and the queue is then empty")
{
	CommandQueue queue;
	auto first = std::make_unique<SetThrottle>();
	first->throttle = 0.5f;
	queue.Push(std::move(first));
	queue.Push(std::make_unique<SetThrottle>());
	CHECK(queue.Pending() == 2);
	const auto drained = queue.Drain();
	REQUIRE(drained.size() == 2);
	CHECK(dynamic_cast<const SetThrottle &>(*drained[0]).throttle == 0.5f);
	CHECK(queue.Pending() == 0);
}

TEST_CASE("Sim: a new world is at step zero; stepping it fails loudly until phase 3")
{
	SimWorld world;
	CHECK(world.Clock().Step() == 0);
	CHECK(world.Registry().storage<entt::entity>().size() == 0);
	CHECK_THROWS_AS(world.Step(1.0 / 60.0), Jettison::Core::NotImplementedError);
}
