#include <jettison/sim/SimWorld.h>

#include <jettison/reflect/Reflect.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace Jettison;
using Reflect::Attribute;

namespace
{
// Rings in the step its count begins: what a system has to run for.
struct Alarm
{
	std::uint64_t at{0};
	bool rung{false};
};
// When the last step began and ended: what a formula of the clock gives.
struct Face
{
	double began{0.0};
	double ended{0.0};
	std::uint64_t steps{0};
};
// Counted every step: what no formula gives.
struct Tally
{
	std::uint64_t count{0};
};
struct Silence
{
	entt::entity alarm{entt::null};
};

constexpr double kStep = 1.0 / 60.0;
constexpr std::uint64_t kNever = std::numeric_limits<std::uint64_t>::max();


void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<Alarm>("FastForwardTests.Alarm")
		    .Field<&Alarm::at>("at", Attribute::Saved)
		    .Field<&Alarm::rung>("rung", Attribute::Saved);
		RegisterType<Face>("FastForwardTests.Face")
		    .Field<&Face::began>("began", Attribute::Saved)
		    .Field<&Face::ended>("ended", Attribute::Saved)
		    .Field<&Face::steps>("steps", Attribute::Saved);
		RegisterType<Tally>("FastForwardTests.Tally").Field<&Tally::count>("count", Attribute::Saved);
		RegisterType<Silence>("FastForwardTests.Silence")
		    .Field<&Silence::alarm>("alarm", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


// Alarms at the steps given and a face, and a tally if asked for.
void Populate(Sim::SimWorld &world, const std::vector<std::uint64_t> &alarms, bool tally = false)
{
	RegisterTypes();
	world.RegisterComponent<Alarm>();
	world.RegisterComponent<Face>();
	world.RegisterComponent<Tally>();
	world.RegisterCommand<Silence>([](Sim::SimWorld &target, const Silence &command)
	                               { target.Registry().get<Alarm>(command.alarm).rung = true; });
	world.RegisterSystem(
	    {"alarm",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<>().Writing<Alarm>(),
	     {},
	     [&world](entt::registry &registry, double)
	     {
		     for (const auto [entity, alarm] : registry.view<Alarm>().each())
			     if (!alarm.rung && alarm.at == world.Clock().Step())
				     alarm.rung = true;
	     }},
	    {.next = [](const Sim::SimWorld &target)
	     {
		     std::uint64_t next = kNever;
		     for (const auto [entity, alarm] : target.Registry().view<const Alarm>().each())
			     if (!alarm.rung)
				     next = std::min(next, alarm.at);
		     return next;
	     }});
	world.RegisterSystem(
	    {"face",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<>().Writing<Face>(),
	     {"alarm"},
	     [&world](entt::registry &registry, double fixedStep)
	     {
		     for (const auto [entity, face] : registry.view<Face>().each())
			     face = {world.Clock().Now(), world.Clock().Now() + fixedStep, face.steps + 1};
	     }},
	    {[](const Sim::SimWorld &) { return kNever; },
	     [](Sim::SimWorld &target, std::uint64_t passed, double lastBegan)
	     {
		     for (const auto [entity, face] : target.Registry().view<Face>().each())
			     face = {lastBegan, target.Clock().Now(), face.steps + passed};
	     }});
	if (tally)
		world.RegisterSystem(
		    {"tally",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<>().Writing<Tally>(),
		     {},
		     [](entt::registry &registry, double)
		     {
			     for (const auto [entity, counted] : registry.view<Tally>().each())
				     ++counted.count;
		     }});

	entt::registry &registry = world.Registry();
	for (const std::uint64_t at : alarms)
		registry.emplace<Alarm>(registry.create(), at, false);
	const entt::entity face = registry.create();
	registry.emplace<Face>(face);
	registry.emplace<Tally>(face);
}
} // namespace


TEST_CASE("Sim: a fast-forward passes over quiet steps to the state stepping through them leaves")
{
	Sim::SimWorld forwarded{kStep, 3};
	Sim::SimWorld stepped{kStep, 3};
	Populate(forwarded, {1000, 25000, 25001});
	Populate(stepped, {1000, 25000, 25001});

	// To each alarm's step, then the one it rings in, and on to the end: seven calls for 40,000 steps.
	constexpr std::uint64_t kEnd = 40000;
	std::vector<std::uint64_t> reached;
	while (forwarded.Clock().Step() < kEnd)
	{
		const std::uint64_t from = forwarded.Clock().Step();
		CHECK(forwarded.FastForward(kEnd) == forwarded.Clock().Step() - from);
		reached.push_back(forwarded.Clock().Step());
		while (stepped.Clock().Step() < forwarded.Clock().Step())
			stepped.Step();
		REQUIRE(forwarded.Save() == stepped.Save());
	}
	CHECK(reached == std::vector<std::uint64_t>{1000, 1001, 25000, 25001, 25002, kEnd});
	CHECK(forwarded.Clock().Now() == stepped.Clock().Now());
	CHECK(forwarded.FastForward(kEnd) == 0);
	CHECK(forwarded.FastForward(kEnd - 1) == 0);
}


TEST_CASE("Sim: a fast-forward with a command queued runs one step")
{
	Sim::SimWorld world{kStep, 3};
	Populate(world, {5000});
	const entt::entity alarm = world.Registry().view<Alarm>().front();
	world.Commands().Push(Silence{alarm});
	CHECK(world.FastForward(10000) == 1);
	CHECK(world.Registry().get<Alarm>(alarm).rung);
	// Silenced, nothing is left to run for.
	CHECK(world.FastForward(10000) == 9999);
}


TEST_CASE("Sim: a system without a quiet has every step run")
{
	Sim::SimWorld world{kStep, 3};
	Populate(world, {}, true);
	for (int call = 0; call < 10; ++call)
		CHECK(world.FastForward(1000) == 1);
	CHECK(
	    world.Registry().view<Tally>().get<Tally>(world.Registry().view<Tally>().front()).count == 10);
}


TEST_CASE("Sim: a fast-forward throws for a system ordered after one never registered, as a step does")
{
	Sim::SimWorld world{kStep, 3};
	Populate(world, {});
	world.RegisterSystem(
	    {"orphan", Ecs::TickPhase::Simulation, {}, {"missing"}, [](entt::registry &, double) {}},
	    {.next = [](const Sim::SimWorld &) { return std::numeric_limits<std::uint64_t>::max(); }});
	CHECK_THROWS_AS(world.FastForward(100), std::logic_error);
	CHECK(world.Clock().Step() == 0);
	CHECK_THROWS_AS(world.Step(), std::logic_error);
}
