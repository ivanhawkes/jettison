#include <jettison/ecs/Scheduler.h>
#include <jettison/ecs/StateMachine.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

using namespace Jettison::Ecs;

TEST_CASE("Ecs: the phases are in frame order")
{
	CHECK(TickPhase::Input < TickPhase::Commands);
	CHECK(TickPhase::Commands < TickPhase::Simulation);
	CHECK(TickPhase::Simulation < TickPhase::Sync);
	CHECK(TickPhase::Sync < TickPhase::Presentation);
	CHECK(TickPhase::Presentation < TickPhase::Render);
}

TEST_CASE("Ecs: systems register; running them fails loudly until phase 3")
{
	Scheduler scheduler;
	scheduler.Register({"animation", TickPhase::Presentation, {}, {}, [](entt::registry &, double) {}});
	CHECK(scheduler.Systems().size() == 1);
	entt::registry registry;
	CHECK_THROWS_AS(
	    scheduler.Run(TickPhase::Presentation, registry, 0.016), Jettison::Core::NotImplementedError);
}

TEST_CASE("Ecs: a state machine starts in its initial state")
{
	enum class Flight
	{
		Docked,
		Flying
	};
	enum class Order
	{
		Undock
	};
	const StateMachine<Flight, Order> machine{Flight::Docked};
	CHECK(machine.Current() == Flight::Docked);
}
