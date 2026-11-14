#include "Systems.h"

#include <jettison/ai/Cruise.h>
#include <jettison/ai/Gates.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Types.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
const std::uint64_t kVesper = Sim::SystemId("Vesper");
constexpr std::int64_t kToll = 2000;
// Two minutes, at 60 steps a second.
constexpr std::uint64_t kClearanceSteps = 7200;
constexpr Sim::Controller kPilot{Sim::ControllerKind::Player, 1};

// What the test's owners pay tolls from: the ship's own, where a game's is its owner's.
struct Credits
{
	std::int64_t amount{0};
};

// A world as a game's start-up makes one: physics, the lanes and the gates, given the sample galaxy,
// the lane from Halcyon to Vesper tolled, and a rule charging a ship's credits; and Halcyon's and
// Vesper's gates made.
struct Fixture
{
	Sim::SimWorld world{kStep, 7};
	Physics::PhysicsWorld &physics;
	Ai::LaneTravel &lanes;
	Ai::Gates &gates;

	Fixture()
	    : physics{Setup(world)}, lanes{Ai::LaneTravel::Attach(world, physics)},
	      gates{Ai::Gates::Attach(world, physics, lanes)}
	{
		static const bool registered = []
		{
			Reflect::RegisterType<Credits>("AiTests.Credits")
			    .Field<&Credits::amount>("amount", Reflect::Attribute::Saved);
			return true;
		}();
		(void)registered;
		world.RegisterComponent<Credits>();
		Physics::ShapeDefinition hull;
		hull.halfExtents = {5.0f, 2.0f, 8.0f};
		physics.DefineShape("hull", hull);
		Physics::ShapeDefinition wide;
		wide.halfExtents = {600.0f, 10.0f, 10.0f};
		physics.DefineShape("wide", wide);
		for (const Celestial::StarSystem &system :
		     {AiTests::Halcyon(), AiTests::Vesper(), AiTests::Marrow()})
		{
			physics.DefineStarSystem(system);
			lanes.DefineStarSystem(system);
			gates.DefineStarSystem(system);
		}
		Celestial::Galaxy galaxy = AiTests::Sample();
		galaxy.lanes[0].toll = kToll;
		lanes.SetGalaxy(std::move(galaxy));
		gates.SetTollRule(
		    {[](const entt::registry &registry, entt::entity ship,
		        std::int64_t toll) -> std::optional<Sim::Reason>
		     {
			     const auto *credits = registry.try_get<Credits>(ship);
			     if (!credits || credits->amount < toll)
				     return Sim::Reason{"reason.test.short", {Sim::ReasonArgument::Integer(toll)}};
			     return std::nullopt;
		     },
		     [](entt::registry &registry, entt::entity ship, std::int64_t toll)
		     { registry.get<Credits>(ship).amount -= toll; }});
		gates.MakeGates(world, kHalcyon);
		gates.MakeGates(world, kVesper);
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		return Physics::PhysicsWorld::Attach(world);
	}

	entt::entity Gate(std::string_view jumpPoint = "Halcyon Gate") const
	{
		return Ai::Gates::FindGate(world.Registry(), kHalcyon, jumpPoint);
	}

	// Which way Halcyon Gate faces now, a ship leaving it looking along its -Z.
	glm::dquat Facing() const
	{
		const Celestial::StarSystem halcyon = AiTests::Halcyon();
		return Celestial::JumpPointFacing(
		    halcyon, static_cast<std::size_t>(halcyon.Find("Halcyon Gate")), world.Clock().Now());
	}

	// A ship of the pilot's in Halcyon Gate's frame, so far behind its ring along the axis (ahead of
	// it, for a negative distance), flying along the axis at that speed (through it, for a positive
	// one), and the credits.
	entt::entity Ship(
	    double behind, float speed, std::int64_t credits = 10000, std::string_view shape = "hull")
	{
		entt::registry &registry = world.Registry();
		const glm::dquat facing = Facing();
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(
		    ship, Sim::Transform{
		              physics.FrameOrigin(kHalcyon, "Halcyon Gate", world.Clock().Now()) +
		                  facing * glm::dvec3{0.0, 0.0, behind},
		              facing});
		registry.emplace<Sim::InSystem>(ship, kHalcyon);
		registry.emplace<Physics::LocalFrame>(ship, "Halcyon Gate");
		Physics::Body body;
		body.kind = Physics::BodyKind::Dynamic;
		body.shape = std::string{shape};
		body.mass = 20000.0f;
		registry.emplace<Physics::Body>(ship, body);
		registry.emplace<Physics::Velocity>(
		    ship, glm::vec3{facing * glm::dvec3{0.0, 0.0, -speed}}, glm::vec3{0.0f});
		registry.emplace<Sim::ControlledBy>(ship, kPilot);
		registry.emplace<Credits>(ship, credits);
		return ship;
	}

	void Run(std::uint64_t steps)
	{
		for (std::uint64_t step = 0; step < steps; ++step)
			world.Step();
	}

	// Steps until the ship is on a lane, or the steps run out; the steps taken.
	std::uint64_t RunUntilOnLane(entt::entity ship, std::uint64_t most)
	{
		std::uint64_t steps = 0;
		while (steps < most && !world.Registry().all_of<Ai::OnLane>(ship))
		{
			world.Step();
			++steps;
		}
		return steps;
	}

	template <typename T> std::vector<T> Events()
	{
		std::vector<T> found;
		for (const Sim::SimEvent &event : world.Events().Take())
			if (const T *value = event.event.try_cast<T>())
				found.push_back(*value);
		return found;
	}

	// Every event of the kind among those raised, the rest dropped with them.
	template <typename T> std::vector<T> EventsOf(const std::vector<Sim::SimEvent> &events) const
	{
		std::vector<T> found;
		for (const Sim::SimEvent &event : events)
			if (const T *value = event.event.try_cast<T>())
				found.push_back(*value);
		return found;
	}

	std::int64_t CreditsOf(entt::entity ship) const
	{
		return world.Registry().get<Credits>(ship).amount;
	}
	const Ai::Gate &GateOf(entt::entity gate) const
	{
		return world.Registry().get<Ai::Gate>(gate);
	}
};
} // namespace

TEST_CASE("Gates: each jump point has a ring, a sensor on its rails facing as it does")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity gate = fixture.Gate();
	REQUIRE(gate != entt::null);
	CHECK(Ai::Gates::FindGate(registry, kVesper, "Vesper Gate") != entt::null);
	CHECK(Ai::Gates::FindGate(registry, kVesper, "Vesper Deep Gate") != entt::null);
	CHECK(Ai::Gates::FindGate(registry, kHalcyon, "Vesper Gate") == entt::null);
	CHECK_THROWS_AS(fixture.gates.MakeGates(fixture.world, kHalcyon), std::logic_error);

	const Physics::Body &body = registry.get<Physics::Body>(gate);
	CHECK(body.kind == Physics::BodyKind::Kinematic);
	CHECK(body.sensor);
	CHECK(body.shape == "rings/500");
	CHECK(registry.get<Physics::LocalFrame>(gate).body == "Halcyon Gate");

	// Its axis along the way the gate faces, as the jump point turns.
	fixture.Run(600);
	const glm::dvec3 axis = registry.get<Sim::Transform>(gate).orientation * glm::dvec3{0.0, 1.0, 0.0};
	CHECK(glm::dot(axis, fixture.Facing() * glm::dvec3{0.0, 0.0, -1.0}) == doctest::Approx(1.0));
	CHECK(fixture.physics.ShapeRadius(body.shape) == doctest::Approx(std::hypot(500.0, 30.0)));
}

TEST_CASE("Gates: a cleared ship flown through the ring is sent, and its owner charged the toll")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity gate = fixture.Gate();
	// 400 m short of the ring at 100 m/s.
	const entt::entity ship = fixture.Ship(400.0, 100.0f);
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, ship, gate});
	const std::uint64_t asked = fixture.world.Clock().Step();
	fixture.Run(1);
	const std::vector<Ai::TransitCleared> cleared = fixture.Events<Ai::TransitCleared>();
	REQUIRE(cleared.size() == 1);
	CHECK(cleared[0].ship == ship);
	CHECK(cleared[0].gate == gate);
	CHECK(cleared[0].toll == kToll);
	CHECK(cleared[0].lapses == asked + kClearanceSteps);
	CHECK(fixture.GateOf(gate).cleared == ship);
	CHECK(registry.get<Ai::Transit>(ship) == Ai::Transit{gate, true});
	// Nothing is charged until it enters.
	CHECK(fixture.CreditsOf(ship) == 10000);

	// Into the ring's 30 m near side, 370 m on, in the step that takes it past.
	const std::uint64_t steps = fixture.RunUntilOnLane(ship, 600);
	CHECK(steps == doctest::Approx(222.0).epsilon(0.02));
	const std::vector<Sim::SimEvent> events = fixture.world.Events().Take();
	const std::vector<Ai::TransitSent> sent = fixture.EventsOf<Ai::TransitSent>(events);
	REQUIRE(sent.size() == 1);
	CHECK(sent[0].ship == ship);
	CHECK(sent[0].toll == kToll);
	CHECK(fixture.EventsOf<Ai::LaneEntered>(events).size() == 1);
	CHECK(fixture.EventsOf<Ai::GatePassed>(events).empty());
	CHECK(fixture.CreditsOf(ship) == 10000 - kToll);
	CHECK(registry.get<Ai::OnLane>(ship).departure == "Halcyon Gate");
	CHECK_FALSE(registry.all_of<Ai::Transit>(ship));
	CHECK(fixture.GateOf(gate).cleared == entt::null);

	// Out at Vesper Gate 40 s on, ahead of its ring, which does not see it pass.
	fixture.Run(2400 + 120);
	CHECK(registry.get<Sim::InSystem>(ship).system == kVesper);
	CHECK(fixture.Events<Ai::GatePassed>().empty());
	const entt::entity far = Ai::Gates::FindGate(registry, kVesper, "Vesper Gate");
	const glm::dvec3 out =
	    registry.get<Sim::Transform>(ship).position - registry.get<Sim::Transform>(far).position;
	const glm::dvec3 ahead = registry.get<Sim::Transform>(far).orientation * glm::dvec3{0.0, 1.0, 0.0};
	CHECK(glm::dot(out, ahead) > 30.0);
	CHECK(fixture.physics.Touching(far).empty());
}

TEST_CASE("Gates: a ship not cleared passes through the ring with a reason, and is charged nothing")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity gate = fixture.Gate();
	const entt::entity ship = fixture.Ship(400.0, 100.0f);
	// Waiting behind another, it is not cleared either.
	const entt::entity first = fixture.Ship(3000.0, 0.0f);
	const entt::entity waiting = fixture.Ship(500.0, 100.0f);
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, first, gate});
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, waiting, gate});
	fixture.Run(1);
	CHECK(fixture.Events<Ai::TransitQueued>().size() == 1);

	std::vector<Ai::GatePassed> passed;
	for (int step = 0; step < 600; ++step)
	{
		fixture.Run(1);
		for (const Ai::GatePassed &each : fixture.Events<Ai::GatePassed>())
			passed.push_back(each);
	}
	REQUIRE(passed.size() == 2);
	CHECK(passed[0].ship == ship);
	CHECK(passed[1].ship == waiting);
	for (const Ai::GatePassed &each : passed)
	{
		CHECK(each.gate == gate);
		CHECK(Sim::Describe(each.reason) == "reason.ai.not_cleared('Halcyon Gate')");
	}
	// Through and out the far side, in Halcyon still, on its credits, and still waiting.
	for (const entt::entity each : {ship, waiting})
	{
		CHECK(registry.get<Sim::InSystem>(each).system == kHalcyon);
		CHECK_FALSE(registry.all_of<Ai::OnLane>(each));
		CHECK(fixture.CreditsOf(each) == 10000);
	}
	const glm::dvec3 through =
	    registry.get<Sim::Transform>(ship).position - registry.get<Sim::Transform>(gate).position;
	CHECK(
	    glm::dot(through, fixture.Facing() * glm::dvec3{0.0, 0.0, -1.0}) ==
	    doctest::Approx(600.0).epsilon(0.01));
	CHECK(registry.get<Ai::Transit>(waiting) == Ai::Transit{gate, false});
	CHECK(fixture.GateOf(gate).cleared == first);
}

TEST_CASE("Gates: a clearance unused lapses, and the gate clears the next in its queue")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity gate = fixture.Gate();
	const entt::entity first = fixture.Ship(3000.0, 0.0f);
	const entt::entity second = fixture.Ship(3500.0, 0.0f);
	const std::uint64_t asked = fixture.world.Clock().Step();
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, first, gate});
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, second, gate});
	fixture.Run(1);
	const std::vector<Ai::TransitQueued> queued = fixture.Events<Ai::TransitQueued>();
	REQUIRE(queued.size() == 1);
	CHECK(queued[0].ship == second);
	CHECK(queued[0].place == 1);
	CHECK(fixture.GateOf(gate).queue == std::vector{second});

	// Held to the step before it lapses.
	fixture.Run(kClearanceSteps - 2);
	CHECK(fixture.Events<Ai::TransitLapsed>().empty());
	CHECK(fixture.GateOf(gate).cleared == first);
	fixture.Run(1);
	CHECK(fixture.world.Clock().Step() == asked + kClearanceSteps);
	const std::vector<Sim::SimEvent> events = fixture.world.Events().Take();
	const std::vector<Ai::TransitLapsed> lapsed = fixture.EventsOf<Ai::TransitLapsed>(events);
	REQUIRE(lapsed.size() == 1);
	CHECK(lapsed[0].ship == first);
	CHECK_FALSE(registry.all_of<Ai::Transit>(first));
	const std::vector<Ai::TransitCleared> next = fixture.EventsOf<Ai::TransitCleared>(events);
	REQUIRE(next.size() == 1);
	CHECK(next[0].ship == second);
	CHECK(next[0].lapses == fixture.world.Clock().Step() - 1 + kClearanceSteps);
	CHECK(fixture.GateOf(gate).cleared == second);
	CHECK(fixture.GateOf(gate).queue.empty());
	CHECK(fixture.CreditsOf(first) == 10000);
}

TEST_CASE("Gates: what cannot be cleared through a gate is refused, saying why")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity gate = fixture.Gate();
	const entt::entity ship = fixture.Ship(3000.0, 0.0f);
	const entt::entity poor = fixture.Ship(3200.0, 0.0f, kToll - 1);
	const entt::entity wide = fixture.Ship(3500.0, 0.0f, 10000, "wide");
	const entt::entity elsewhere = fixture.Ship(3800.0, 0.0f);
	registry.replace<Physics::LocalFrame>(elsewhere, "Ostra Highport");
	const entt::entity gone = registry.create();
	registry.destroy(gone);

	const auto refused = [&](const Ai::RequestTransit &command)
	{
		fixture.world.Commands().Push(command);
		fixture.Run(1);
		const std::vector<Ai::TransitRefused> found = fixture.Events<Ai::TransitRefused>();
		REQUIRE(found.size() == 1);
		CHECK(found[0].ship == command.ship);
		return Sim::Describe(found[0].reason);
	};
	CHECK(refused({kPilot, gone, gate}) == "reason.ai.no_such_entity()");
	CHECK(refused({{Sim::ControllerKind::Player, 2}, ship, gate}) == "reason.ai.not_controller()");
	CHECK(refused({kPilot, ship, ship}) == "reason.ai.not_a_gate()");
	CHECK(refused({kPilot, elsewhere, gate}) == "reason.ai.not_at_gate('Halcyon Gate')");
	CHECK(refused({kPilot, wide, gate}) == "reason.ai.hull_too_large('Halcyon Gate', 500)");
	CHECK(refused({kPilot, poor, gate}) == "reason.test.short(2000)");
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, ship, gate});
	fixture.Run(1);
	CHECK(fixture.Events<Ai::TransitCleared>().size() == 1);
	CHECK(refused({kPilot, ship, gate}) == "reason.ai.already_cleared('Halcyon Gate')");
	CHECK(refused({kPilot, elsewhere, gate}) == "reason.ai.not_at_gate('Halcyon Gate')");
	// Nothing was taken from any of them.
	for (const entt::entity each : {ship, wide, elsewhere})
		CHECK(fixture.CreditsOf(each) == 10000);
}

TEST_CASE("Gates: a gate no lane leaves through clears nothing")
{
	Fixture fixture;
	Celestial::Galaxy galaxy = AiTests::Sample();
	galaxy.lanes.pop_back();
	fixture.lanes.SetGalaxy(std::move(galaxy));
	entt::registry &registry = fixture.world.Registry();
	const entt::entity deep = Ai::Gates::FindGate(registry, kVesper, "Vesper Deep Gate");
	const entt::entity ship = fixture.Ship(3000.0, 0.0f);
	registry.replace<Sim::InSystem>(ship, kVesper);
	registry.replace<Physics::LocalFrame>(ship, "Vesper Deep Gate");
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, ship, deep});
	fixture.Run(1);
	const std::vector<Ai::TransitRefused> refused = fixture.Events<Ai::TransitRefused>();
	REQUIRE(refused.size() == 1);
	CHECK(Sim::Describe(refused[0].reason) == "reason.ai.leads_nowhere('Vesper Deep Gate')");
}

TEST_CASE("Gates: a cleared ship whose owner can no longer pay passes through, and frees the gate")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity gate = fixture.Gate();
	const entt::entity ship = fixture.Ship(400.0, 100.0f);
	const entt::entity next = fixture.Ship(3000.0, 0.0f);
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, ship, gate});
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, next, gate});
	fixture.Run(1);
	fixture.Events<Ai::TransitCleared>();
	// Spent, after it was cleared.
	registry.get<Credits>(ship).amount = 500;

	std::vector<Sim::SimEvent> events;
	for (int step = 0; step < 300 && events.empty(); ++step)
	{
		fixture.Run(1);
		for (Sim::SimEvent &event : fixture.world.Events().Take())
			if (event.event.try_cast<Ai::GatePassed>() || event.event.try_cast<Ai::TransitCleared>())
				events.push_back(std::move(event));
	}
	const std::vector<Ai::GatePassed> passed = fixture.EventsOf<Ai::GatePassed>(events);
	REQUIRE(passed.size() == 1);
	CHECK(passed[0].ship == ship);
	CHECK(Sim::Describe(passed[0].reason) == "reason.test.short(2000)");
	CHECK(fixture.CreditsOf(ship) == 500);
	CHECK_FALSE(registry.all_of<Ai::OnLane>(ship));
	CHECK_FALSE(registry.all_of<Ai::Transit>(ship));
	// The next in the queue cleared in the same step.
	const std::vector<Ai::TransitCleared> cleared = fixture.EventsOf<Ai::TransitCleared>(events);
	REQUIRE(cleared.size() == 1);
	CHECK(cleared[0].ship == next);
}

TEST_CASE("Gates: a clearance is given up as its ship leaves the gate's frame, or goes")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity gate = fixture.Gate();
	const entt::entity leaving = fixture.Ship(3000.0, 0.0f);
	const entt::entity going = fixture.Ship(3500.0, 0.0f);
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, leaving, gate});
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, going, gate});
	fixture.Run(1);
	fixture.world.Events().Take();

	registry.replace<Physics::LocalFrame>(leaving, "Ostra Highport");
	fixture.Run(1);
	std::vector<Ai::TransitLapsed> lapsed = fixture.Events<Ai::TransitLapsed>();
	REQUIRE(lapsed.size() == 1);
	CHECK(lapsed[0].ship == leaving);
	CHECK_FALSE(registry.all_of<Ai::Transit>(leaving));
	CHECK(fixture.GateOf(gate).cleared == going);

	registry.destroy(going);
	fixture.Run(1);
	lapsed = fixture.Events<Ai::TransitLapsed>();
	REQUIRE(lapsed.size() == 1);
	CHECK(lapsed[0].ship == going);
	CHECK(fixture.GateOf(gate).cleared == entt::null);
}

TEST_CASE("Gates: a world saved with a ship cleared and one waiting goes on as the one saved does")
{
	Fixture fixture;
	const entt::entity gate = fixture.Gate();
	const entt::entity ship = fixture.Ship(1500.0, 100.0f);
	const entt::entity next = fixture.Ship(3000.0, 0.0f);
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, ship, gate});
	fixture.world.Commands().Push(Ai::RequestTransit{kPilot, next, gate});
	fixture.Run(300);
	const std::vector<std::byte> saved = fixture.world.Save();
	fixture.Run(1200);
	CHECK(fixture.world.Registry().all_of<Ai::OnLane>(ship));
	CHECK(fixture.GateOf(gate).cleared == next);
	const std::uint64_t hash = fixture.world.StateHash();

	Fixture loaded;
	loaded.world.Load(saved);
	CHECK(loaded.GateOf(gate).cleared == ship);
	CHECK(loaded.GateOf(gate).queue == std::vector{next});
	loaded.Run(1200);
	CHECK(loaded.world.StateHash() == hash);
	CHECK(loaded.CreditsOf(ship) == 10000 - kToll);
}
