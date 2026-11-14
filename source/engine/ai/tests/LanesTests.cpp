#include "Systems.h"

#include <jettison/ai/Gates.h>
#include <jettison/ai/Lanes.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
const std::uint64_t kVesper = Sim::SystemId("Vesper");
// 40 s from Halcyon to Vesper, at 60 steps a second.
constexpr std::uint64_t kTransitSteps = 2400;

// A world as a game's start-up makes one: physics, then lane travel, both given the sample galaxy.
struct Fixture
{
	Sim::SimWorld world{kStep, 7};
	Physics::PhysicsWorld &physics;
	Ai::LaneTravel &lanes;

	explicit Fixture(bool withGalaxy = true)
	    : physics{Setup(world)}, lanes{Ai::LaneTravel::Attach(world, physics)}
	{
		Physics::ShapeDefinition hull;
		hull.halfExtents = {5.0f, 2.0f, 8.0f};
		physics.DefineShape("hull", hull);
		for (Celestial::StarSystem system : {AiTests::Halcyon(), AiTests::Vesper(), AiTests::Marrow()})
		{
			physics.DefineStarSystem(system);
			if (withGalaxy)
				lanes.DefineStarSystem(std::move(system));
		}
		if (withGalaxy)
			lanes.SetGalaxy(AiTests::Sample());
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		return Physics::PhysicsWorld::Attach(world);
	}

	// A ship beside Ostra Highport, in its frame, flying away from it at 50 m/s with its controls
	// taken against the station: a ship flying into the gate.
	entt::entity Ship()
	{
		entt::registry &registry = world.Registry();
		const Celestial::StarSystem halcyon = AiTests::Halcyon();
		const glm::dvec3 station =
		    Celestial::EvaluateBody(
		        halcyon, static_cast<std::size_t>(halcyon.Find("Ostra Highport")), 0.0)
		        .metres;
		const entt::entity hub = registry.create();
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(
		    ship,
		    Sim::Transform{station + glm::dvec3{0.0, 0.0, 200.0}, glm::dquat{0.6, 0.0, 0.8, 0.0}});
		registry.emplace<Sim::InSystem>(ship, kHalcyon);
		registry.emplace<Physics::LocalFrame>(ship, "Ostra Highport");
		Physics::Body body;
		body.kind = Physics::BodyKind::Dynamic;
		body.shape = "hull";
		body.mass = 20000.0f;
		registry.emplace<Physics::Body>(ship, body);
		registry.emplace<Physics::Velocity>(ship, glm::vec3{0.0f, 0.0f, 50.0f}, glm::vec3{0.0f});
		registry.emplace<Physics::FlightControls>(ship, Physics::FlightControls{{}, {}, false, hub});
		return ship;
	}

	void Run(std::uint64_t steps)
	{
		for (std::uint64_t step = 0; step < steps; ++step)
			world.Step();
	}

	template <typename T> std::vector<T> Events()
	{
		std::vector<T> found;
		for (const Sim::SimEvent &event : world.Events().Take())
			if (const T *value = event.event.try_cast<T>())
				found.push_back(*value);
		return found;
	}

	std::uint64_t SystemOf(entt::entity entity) const
	{
		return world.Registry().get<Sim::InSystem>(entity).system;
	}
};

glm::dvec3 Where(const Celestial::StarSystem &system, std::string_view body, double time)
{
	return Celestial::EvaluateBody(system, static_cast<std::size_t>(system.Find(body)), time).metres;
}
} // namespace

TEST_CASE("Lanes: a ship enters the gate, is in no system for the transit, and leaves the far one")
{
	Fixture fixture;
	const entt::entity ship = fixture.Ship();
	fixture.Run(10);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 1);
	fixture.Events<Ai::LaneEntered>();

	const std::uint64_t entered = fixture.world.Clock().Step();
	fixture.world.Commands().Push(Ai::EnterLane{ship, "Halcyon Gate"});
	fixture.Run(1);
	const std::vector<Ai::LaneEntered> entries = fixture.Events<Ai::LaneEntered>();
	REQUIRE(entries.size() == 1);
	CHECK(entries[0].entity == ship);
	CHECK(entries[0].lane.from == kHalcyon);
	CHECK(entries[0].lane.departure == "Halcyon Gate");
	CHECK(entries[0].lane.to == kVesper);
	CHECK(entries[0].lane.arrival == "Vesper Gate");
	CHECK(entries[0].lane.arrivalStep == entered + kTransitSteps);
	CHECK(entries[0].transitSeconds == 40.0);
	CHECK(fixture.SystemOf(ship) == Sim::kNoSystem);
	CHECK(fixture.world.Registry().all_of<Ai::OnLane>(ship));
	// Physics removed its body in the step it entered.
	CHECK(fixture.physics.BodyCount() == 0);

	// One step short of the transit it is still on the lane; the step that ends it brings it out.
	fixture.Run(kTransitSteps - 2);
	CHECK(fixture.SystemOf(ship) == Sim::kNoSystem);
	CHECK(fixture.Events<Ai::LaneLeft>().empty());
	fixture.Run(1);
	CHECK(fixture.world.Clock().Step() == entered + kTransitSteps);
	const std::vector<Ai::LaneLeft> exits = fixture.Events<Ai::LaneLeft>();
	REQUIRE(exits.size() == 1);
	CHECK(exits[0].entity == ship);
	CHECK(exits[0].system == kVesper);
	CHECK(exits[0].arrival == "Vesper Gate");

	entt::registry &registry = fixture.world.Registry();
	CHECK(fixture.SystemOf(ship) == kVesper);
	CHECK_FALSE(registry.all_of<Ai::OnLane>(ship));
	// In the frame of the refinery the gate keeps station by, so it can fly to it and dock.
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Corva Refinery");
	CHECK(registry.get<Physics::FlightControls>(ship).reference == entt::null);
	const Celestial::StarSystem vesper = AiTests::Vesper();
	const double arrivedAt = fixture.world.Clock().Now();
	const auto gate = static_cast<std::size_t>(vesper.Find("Vesper Gate"));
	// Ahead of the ring, clear of its sensor by its hull and the clearance, looking the way it faces.
	const glm::dquat facing = Celestial::JumpPointFacing(vesper, gate, arrivedAt);
	const double outAhead = Ai::Gates::kRingDepth / 2.0 + fixture.physics.ShapeRadius("hull") +
	                        Ai::LaneTravel::kArrivalClearance;
	CHECK(
	    glm::distance(
	        registry.get<Sim::Transform>(ship).position,
	        Where(vesper, "Vesper Gate", arrivedAt) + facing * glm::dvec3{0.0, 0.0, -outAhead}) < 1e-6);
	CHECK(registry.get<Sim::Transform>(ship).orientation == facing);
	// At rest, whatever it entered the far gate at.
	CHECK(registry.get<Physics::Velocity>(ship) == Physics::Velocity{});

	// Physics makes its body in the refinery's frame in the next step, and it stays where it came
	// out, 60 m ahead of the gate, 5 km from the refinery and looking at it, rather than flying on at
	// 50 m/s.
	fixture.Run(60);
	CHECK(fixture.physics.BodyCount(kVesper) == 1);
	CHECK(fixture.physics.FrameCount(kVesper) == 1);
	const glm::dvec3 now = registry.get<Sim::Transform>(ship).position;
	const double time = fixture.world.Clock().Now();
	// The gate turns about the refinery with the refinery's orbit, 5 km x 2 pi in 29 206 s, 1.08 m/s
	// in the refinery's frame, which does not turn; the ship is still in it.
	const glm::dvec3 cameOut =
	    Where(vesper, "Vesper Gate", arrivedAt) + facing * glm::dvec3{0.0, 0.0, -outAhead};
	CHECK(
	    glm::length(
	        (now - Where(vesper, "Vesper Gate", time)) -
	        (cameOut - Where(vesper, "Vesper Gate", arrivedAt))) ==
	    doctest::Approx(1.08).epsilon(0.02));
	const glm::dvec3 toRefinery = Where(vesper, "Corva Refinery", time) - now;
	CHECK(glm::length(toRefinery) == doctest::Approx(5000.0 - outAhead).epsilon(0.01));
	const glm::dvec3 ahead =
	    registry.get<Sim::Transform>(ship).orientation * glm::dvec3{0.0, 0.0, -1.0};
	CHECK(glm::dot(ahead, glm::normalize(toRefinery)) > 0.999);
}

TEST_CASE("Lanes: what no lane leaves through, or nothing in a system, is refused and left as it was")
{
	Fixture fixture;
	const entt::entity ship = fixture.Ship();
	const entt::entity drifting = fixture.world.Registry().create();
	fixture.world.Registry().emplace<Sim::InSystem>(drifting, Sim::kNoSystem);
	const entt::entity gone = fixture.world.Registry().create();
	fixture.world.Registry().destroy(gone);

	fixture.world.Commands().Push(Ai::EnterLane{ship, "Ostra Highport"});
	fixture.world.Commands().Push(Ai::EnterLane{ship, "Vesper Gate"});
	fixture.world.Commands().Push(Ai::EnterLane{drifting, "Halcyon Gate"});
	fixture.world.Commands().Push(Ai::EnterLane{gone, "Halcyon Gate"});
	fixture.Run(1);
	const std::vector<Ai::LaneRefused> refused = fixture.Events<Ai::LaneRefused>();
	REQUIRE(refused.size() == 4);
	CHECK(refused[0].entity == ship);
	CHECK(Sim::Describe(refused[0].reason) == "reason.ai.no_lane('Halcyon', 'Ostra Highport')");
	CHECK(Sim::Describe(refused[1].reason) == "reason.ai.no_lane('Halcyon', 'Vesper Gate')");
	CHECK(refused[2].reason == Sim::MakeReason(Ai::Reason::InNoSystem));
	CHECK(refused[3].reason == Sim::MakeReason(Ai::Reason::NoSuchEntity));
	CHECK(fixture.SystemOf(ship) == kHalcyon);
	CHECK_FALSE(fixture.world.Registry().all_of<Ai::OnLane>(ship));

	// Once on a lane, it cannot enter another.
	fixture.world.Commands().Push(Ai::EnterLane{ship, "Halcyon Gate"});
	fixture.world.Commands().Push(Ai::EnterLane{ship, "Halcyon Gate"});
	fixture.Run(1);
	const std::vector<Ai::LaneRefused> twice = fixture.Events<Ai::LaneRefused>();
	REQUIRE(twice.size() == 1);
	CHECK(twice[0].reason == Sim::MakeReason(Ai::Reason::OnLane));

	// A world given no galaxy has no lanes.
	Fixture bare{false};
	const entt::entity alone = bare.Ship();
	bare.world.Commands().Push(Ai::EnterLane{alone, "Halcyon Gate"});
	bare.Run(1);
	const std::vector<Ai::LaneRefused> none = bare.Events<Ai::LaneRefused>();
	REQUIRE(none.size() == 1);
	CHECK(none[0].reason == Sim::MakeReason(Ai::Reason::NoGalaxy));
}

TEST_CASE("Lanes: a world saved mid-transit arrives as the one saved does, and a replay with it")
{
	Fixture fixture;
	const entt::entity ship = fixture.Ship();
	fixture.world.SetRecording(true);
	const std::vector<std::byte> start = fixture.world.Save();
	fixture.Run(30);
	fixture.world.Commands().Push(Ai::EnterLane{ship, "Halcyon Gate"});
	fixture.Run(1000);
	const std::vector<std::byte> midway = fixture.world.Save();
	fixture.Run(kTransitSteps);
	CHECK(fixture.SystemOf(ship) == kVesper);
	const std::uint64_t arrived = fixture.world.StateHash();

	Fixture loaded;
	loaded.world.Load(midway);
	CHECK(loaded.SystemOf(ship) == Sim::kNoSystem);
	loaded.Run(kTransitSteps);
	CHECK(loaded.SystemOf(ship) == kVesper);
	CHECK(loaded.world.StateHash() == arrived);

	Fixture replay;
	replay.world.Load(start);
	const std::vector<Sim::AppliedCommand> &recorded = fixture.world.Recorded();
	std::size_t next = 0;
	while (replay.world.Clock().Step() < fixture.world.Clock().Step())
	{
		while (next < recorded.size() && recorded[next].step == replay.world.Clock().Step())
			replay.world.Commands().Push(recorded[next++].command);
		replay.world.Step();
	}
	CHECK(replay.world.StateHash() == arrived);
}

TEST_CASE("Lanes: a transit shorter than a step takes one, and arrives as the step it entered ends")
{
	Fixture fixture;
	Celestial::Galaxy instant = AiTests::Sample();
	instant.lanes[1].transitSeconds = 0.0;
	fixture.lanes.SetGalaxy(instant);
	entt::registry &registry = fixture.world.Registry();
	const entt::entity probe = registry.create();
	registry.emplace<Sim::Transform>(probe);
	registry.emplace<Sim::InSystem>(probe, kVesper);

	fixture.world.Commands().Push(Ai::EnterLane{probe, "Vesper Deep Gate"});
	fixture.Run(1);
	CHECK(fixture.SystemOf(probe) == Sim::SystemId("Marrow"));
	// Not a body, so given no frame.
	CHECK_FALSE(registry.all_of<Physics::LocalFrame>(probe));
	CHECK(
	    registry.get<Sim::Transform>(probe).position ==
	    Where(AiTests::Marrow(), "Marrow Gate", fixture.world.Clock().Now()));
	CHECK_FALSE(registry.all_of<Physics::Velocity>(probe));
	const std::vector<Sim::SimEvent> events = fixture.world.Events().Take();
	REQUIRE(events.size() == 2);
	CHECK(events[0].event.try_cast<Ai::LaneEntered>());
	CHECK(events[1].event.try_cast<Ai::LaneLeft>());
}

TEST_CASE(
    "Lanes: ships coming out of a gate together, or onto one still there, come out clear of each other")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity first = fixture.Ship();
	const entt::entity second = fixture.Ship();
	const entt::entity third = fixture.Ship();
	// Two in one step, and the third a second later, while they still sit where they came out.
	fixture.world.Commands().Push(Ai::EnterLane{first, "Halcyon Gate"});
	fixture.world.Commands().Push(Ai::EnterLane{second, "Halcyon Gate"});
	fixture.Run(60);
	fixture.world.Commands().Push(Ai::EnterLane{third, "Halcyon Gate"});
	fixture.Run(kTransitSteps - 60);
	REQUIRE(fixture.SystemOf(first) == kVesper);
	REQUIRE(fixture.SystemOf(second) == kVesper);
	CHECK(fixture.SystemOf(third) == Sim::kNoSystem);

	const Celestial::StarSystem vesper = AiTests::Vesper();
	const auto at = [&](entt::entity ship) { return registry.get<Sim::Transform>(ship).position; };
	// The first ahead of the ring, as a ship alone would be; the second on the first ring about it,
	// across the way the gate faces, a hull's breadth and the clearance out.
	const double hull = fixture.physics.ShapeRadius("hull");
	const double spacing = 2.0 * hull + Ai::LaneTravel::kArrivalClearance;
	const double arrived = fixture.world.Clock().Now();
	const glm::dvec3 gate = Where(vesper, "Vesper Gate", arrived);
	const glm::dquat facing = Celestial::JumpPointFacing(
	    vesper, static_cast<std::size_t>(vesper.Find("Vesper Gate")), arrived);
	CHECK(glm::distance(at(second), at(first)) == doctest::Approx(spacing).epsilon(0.01));
	CHECK(std::abs(glm::dot(at(second) - at(first), facing * glm::dvec3{0.0, 0.0, -1.0})) < 0.5);
	const double ahead = Ai::Gates::kRingDepth / 2.0 + hull + Ai::LaneTravel::kArrivalClearance;
	CHECK(glm::distance(at(first), gate + facing * glm::dvec3{0.0, 0.0, -ahead}) < 1e-3);

	fixture.Run(60);
	REQUIRE(fixture.SystemOf(third) == kVesper);
	for (const entt::entity other : {first, second})
		CHECK(
		    glm::distance(at(third), at(other)) >=
		    2.0 * hull + Ai::LaneTravel::kArrivalClearance - 1e-6);
	// Nothing came out inside another.
	fixture.Run(30);
	for (const Physics::ContactEvent &contact : fixture.physics.Contacts())
		CHECK(contact.phase != Physics::ContactPhase::Began);
}
