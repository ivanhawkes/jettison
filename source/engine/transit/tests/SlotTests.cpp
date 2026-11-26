#include <jettison/transit/Slot.h>

#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/transit/Path.h>
#include <jettison/transit/Portal.h>
#include <jettison/transit/Types.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

// Slots as a game of no star system has them: a car park's bays, each held by one car for the time
// it paid for while others wait, and a toll booth's gated portal, whose barrier sends the car holding
// it through to the motorway beyond and lets any other that drives into it pass, unsent.
using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kTown = Sim::FrameId("Town");

// Seconds a car pays for, at a bay or a booth.
constexpr double kPaid = 10.0;

// Seconds through the booth to the motorway beyond.
constexpr double kThrough = 1.0;
const glm::dvec3 kBeyond{0.0, 0.0, -40.0};

// The game: each step, what a car can no longer hold is dropped, the cars holding a booth that touch
// its barrier go through, holds paid for lapse, and each free bay or booth is given to the car first
// in its queue. What happens is logged, as a game's events would say it.
struct Fixture
{
	Sim::SimWorld world{kStep, 7};
	Physics::PhysicsWorld &physics;
	std::vector<std::string> happened;


	Fixture() : physics{Physics::PhysicsWorld::Attach(Setup(world))}
	{
		Physics::ShapeDefinition car;
		car.halfExtents = {1.0f, 1.2f, 1.0f};
		physics.DefineShape("car", car);
		Physics::ShapeDefinition barrier;
		barrier.halfExtents = {6.0f, 3.0f, 3.0f};
		physics.DefineShape("barrier", barrier);
		Transit::Portals::Attach(
		    world, physics,
		    [](std::uint64_t /*frame*/, std::string_view /*exit*/, double /*time*/) -> Transit::Exit
		    { return {{kBeyond, Transit::Facing({0.0, 0.0, -1.0})}, "", 0.0}; });
		world.RegisterSystem(
		    {"booths",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<Physics::Body>()
		         .Writing<
		             Transit::Slot, Transit::Claim, Transit::InPortal, Sim::InFrame,
		             Physics::FlightControls>(),
		     {std::string{Physics::PhysicsWorld::kSystemName}},
		     [this](entt::registry & /*registry*/, double /*fixedStep*/) { Run(); }},
		    // A car holding a booth may touch its barrier in any step.
		    {.next = [](const Sim::SimWorld &target)
		     {
			     const entt::registry &registry = target.Registry();
			     for (const auto [booth, state, sensor] :
			          registry.view<const Transit::Slot, const Physics::Body>().each())
				     if (state.holder != entt::null)
					     return target.Clock().Step();
			     return Transit::NextLapse<Transit::Slot>(target);
		     }});
	}


	static Sim::SimWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		Transit::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
		world.RegisterComponent<Transit::Slot>();
		world.RegisterComponent<Transit::Claim>();
		return world;
	}


	void Log(std::string_view what, entt::entity car, entt::entity slot)
	{
		happened.push_back(
		    std::format("{} {} {}", what, entt::to_integral(car), entt::to_integral(slot)));
	}


	std::uint64_t PaidUntil() const
	{
		return world.Clock().Step() + Transit::Steps(kPaid, kStep);
	}


	// The game's ask: the slot at once if it is free, or else a place in its queue.
	void Ask(entt::entity car, entt::entity slot)
	{
		entt::registry &registry = world.Registry();
		if (Transit::Free(registry.get<Transit::Slot>(slot)))
		{
			Transit::Hold<Transit::Slot, Transit::Claim>(registry, slot, car, PaidUntil());
			Log("holds", car, slot);
		}
		else
			Log(std::format(
			        "waits{}", Transit::Wait<Transit::Slot, Transit::Claim>(registry, slot, car)),
			    car, slot);
	}


	void Run()
	{
		entt::registry &registry = world.Registry();
		const std::vector<entt::entity> slots = Transit::InOrder<Transit::Slot>(registry);
		for (const auto [car, slot] : Transit::DropGone<Transit::Slot>(registry, slots))
			Log("gone", car, slot);
		const Transit::Crossings crossings = Transit::Crossed<Transit::Slot>(registry, physics, slots);
		for (const auto [slot, car] : crossings.through)
		{
			Transit::GiveUp<Transit::Slot, Transit::Claim>(registry, car);
			Transit::Portals::Enter(world, car, "booth", kTown, "beyond", kThrough);
			Log("through", car, slot);
		}
		for (const auto [slot, car] : crossings.passed)
			Log("passed", car, slot);
		for (const auto [car, slot] :
		     Transit::Lapse<Transit::Slot, Transit::Claim>(registry, slots, world.Clock().Step() + 1))
			Log("lapsed", car, slot);
		for (const entt::entity slot : slots)
			Transit::Serve<Transit::Slot, Transit::Claim>(
			    registry, slot,
			    [&](entt::entity car)
			    {
				    Transit::Hold<Transit::Slot, Transit::Claim>(registry, slot, car, PaidUntil());
				    Log("holds", car, slot);
			    });
	}


	entt::entity Bay()
	{
		const entt::entity bay = world.Registry().create();
		world.Registry().emplace<Transit::Slot>(bay);
		return bay;
	}


	entt::entity Booth()
	{
		entt::registry &registry = world.Registry();
		const entt::entity booth = registry.create();
		registry.emplace<Transit::Slot>(booth);
		registry.emplace<Sim::Transform>(booth);
		registry.emplace<Sim::InFrame>(booth, kTown);
		Physics::Body barrier{Physics::BodyKind::Kinematic, "barrier"};
		barrier.sensor = true;
		registry.emplace<Physics::Body>(booth, barrier);
		return booth;
	}


	entt::entity Car(const glm::dvec3 &at)
	{
		entt::registry &registry = world.Registry();
		const entt::entity car = registry.create();
		registry.emplace<Sim::Transform>(car, Sim::Transform{at, glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InFrame>(car, kTown);
		Physics::Body body{Physics::BodyKind::Dynamic, "car"};
		body.mass = 800.0f;
		registry.emplace<Physics::Body>(car, body);
		registry.emplace<Physics::Velocity>(car);
		return car;
	}
};
} // namespace


TEST_CASE("Slot: a car park's bays are held in turn, first come first, forwarded as stepped")
{
	Fixture forwarded;
	Fixture stepped;
	entt::entity first = entt::null;
	entt::entity second = entt::null;
	entt::entity third = entt::null;
	entt::entity across = entt::null;
	entt::entity bay = entt::null;
	entt::entity other = entt::null;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		bay = fixture->Bay();
		other = fixture->Bay();
		first = fixture->world.Registry().create();
		second = fixture->world.Registry().create();
		third = fixture->world.Registry().create();
		across = fixture->world.Registry().create();
		fixture->world.Step();
		fixture->Ask(first, bay);
		fixture->Ask(second, bay);
		fixture->Ask(third, bay);
		fixture->Ask(across, other);
	}
	const entt::registry &registry = stepped.world.Registry();
	CHECK(registry.get<Transit::Claim>(first) == Transit::Claim{bay, true});
	CHECK(registry.get<Transit::Claim>(third) == Transit::Claim{bay, false});
	CHECK(registry.get<Transit::Slot>(bay).queue == std::vector{second, third});
	const std::uint64_t asked = stepped.world.Clock().Step();
	const std::uint64_t paid = Transit::Steps(kPaid, kStep);

	// The second drives off before its turn, and the across bay's car stays the time it paid for.
	const auto drive = [&](std::uint64_t until)
	{
		int calls = 0;
		while (forwarded.world.Clock().Step() < until)
		{
			forwarded.world.FastForward(until);
			++calls;
			while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
				stepped.world.Step();
			REQUIRE(forwarded.world.Save() == stepped.world.Save());
		}
		return calls;
	};
	int calls = drive(asked + 60);
	for (Fixture *fixture : {&forwarded, &stepped})
		fixture->world.Registry().destroy(second);
	calls += drive(asked + paid + 60);
	MESSAGE("Parked for " << kPaid << " s and more in " << calls << " calls");
	CHECK(calls < 20);
	CHECK(forwarded.happened == stepped.happened);
	const auto said = [](entt::entity car, entt::entity slot, std::string_view what)
	{ return std::format("{} {} {}", what, entt::to_integral(car), entt::to_integral(slot)); };
	CHECK(
	    stepped.happened == std::vector<std::string>{
	                            said(first, bay, "holds"), said(second, bay, "waits1"),
	                            said(third, bay, "waits2"), said(across, other, "holds"),
	                            said(second, bay, "gone"), said(first, bay, "lapsed"),
	                            said(across, other, "lapsed"), said(third, bay, "holds")});

	// The third holds the bay from the step the first's lapsed in, for the time it paid for.
	CHECK_FALSE(registry.all_of<Transit::Claim>(first));
	CHECK(registry.get<Transit::Claim>(third) == Transit::Claim{bay, true});
	CHECK(registry.get<Transit::Slot>(bay) == Transit::Slot{third, asked + paid - 1 + paid, {}});
	CHECK(Transit::Free(registry.get<Transit::Slot>(other)));
}


TEST_CASE("Slot: a toll booth's gated portal sends the car holding it through, and others pass")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity booth = fixture.Booth();

	// Three cars already at the barrier: one paid, one waiting its turn, and one that has not paid.
	const entt::entity paid = fixture.Car({-4.0, 0.0, 0.0});
	const entt::entity waiting = fixture.Car({0.0, 0.0, 0.0});
	const entt::entity unpaid = fixture.Car({4.0, 0.0, 0.0});
	fixture.Ask(paid, booth);
	fixture.Ask(waiting, booth);

	// The paid car goes through as the barrier first sees it, and the waiting one, served then, as
	// the step after does, though it was inside already.
	fixture.world.Step();
	CHECK(registry.all_of<Transit::InPortal>(paid));
	CHECK(registry.get<Transit::Claim>(waiting) == Transit::Claim{booth, true});
	fixture.world.Step();
	CHECK(registry.all_of<Transit::InPortal>(waiting));
	CHECK_FALSE(registry.all_of<Transit::Claim>(waiting));
	CHECK(Transit::Free(registry.get<Transit::Slot>(booth)));
	CHECK_FALSE(registry.all_of<Transit::InPortal>(unpaid));
	CHECK(registry.get<Sim::InFrame>(unpaid).frame == kTown);

	const auto said = [&](entt::entity car, std::string_view what)
	{ return std::format("{} {} {}", what, entt::to_integral(car), entt::to_integral(booth)); };
	const std::vector<std::string> &happened = fixture.happened;
	REQUIRE(happened.size() >= 4);
	CHECK(happened[0] == said(paid, "holds"));
	CHECK(happened[1] == said(waiting, "waits1"));
	CHECK(happened[2] == said(paid, "through"));
	CHECK(std::ranges::count(happened, said(unpaid, "passed")) == 1);
	CHECK(std::ranges::count(happened, said(waiting, "through")) == 1);
	CHECK(std::ranges::count(happened, said(paid, "passed")) == 0);

	// Out on the motorway beyond.
	while (registry.all_of<Transit::InPortal>(waiting))
		fixture.world.Step();
	CHECK(registry.get<Sim::InFrame>(paid).frame == kTown);
	CHECK(registry.get<Sim::Transform>(paid).position.z < kBeyond.z);
}
