#include "Systems.h"

#include <jettison/ai/Cruise.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Types.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
// A shuttle's drive (defs/hulls/shuttle.yaml).
constexpr Ai::CruiseDrive kDrive{40.0f, 1.0e8, 0.25f};

// A world as a game's start-up makes one: physics, the lanes, then cruising, given the sample
// galaxy's systems.
struct Fixture
{
	Sim::SimWorld world{kStep, 7};
	Physics::PhysicsWorld &physics;
	Ai::LaneTravel &lanes;
	Ai::CruiseTravel &cruises;

	Fixture()
	    : physics{Setup(world)}, lanes{Ai::LaneTravel::Attach(world, physics)},
	      cruises{Ai::CruiseTravel::Attach(world)}
	{
		Physics::ShapeDefinition hull;
		hull.halfExtents = {3.2f, 0.9f, 4.9f};
		physics.DefineShape("hull", hull);
		for (const Celestial::StarSystem &system :
		     {AiTests::Halcyon(), AiTests::Vesper(), AiTests::Marrow()})
		{
			physics.DefineStarSystem(system);
			lanes.DefineStarSystem(system);
			cruises.DefineStarSystem(system);
		}
		lanes.SetGalaxy(AiTests::Sample());
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		world.RegisterComponent<Sim::ControlledBy>();
		return Physics::PhysicsWorld::Attach(world);
	}

	// A ship at rest in the frame of a body of Halcyon, that far from it along the direction, with a
	// shuttle's drive.
	entt::entity Ship(std::string_view frame, const glm::dvec3 &offset)
	{
		entt::registry &registry = world.Registry();
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(
		    ship,
		    Sim::Transform{Where(frame, world.Clock().Now()) + offset, glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InSystem>(ship, kHalcyon);
		registry.emplace<Physics::LocalFrame>(ship, std::string{frame});
		Physics::Body body;
		body.kind = Physics::BodyKind::Dynamic;
		body.shape = "hull";
		body.mass = 20000.0f;
		registry.emplace<Physics::Body>(ship, body);
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::FlightControls>(ship);
		registry.emplace<Ai::CruiseDrive>(ship, kDrive);
		return ship;
	}

	static glm::dvec3 Where(std::string_view body, double time)
	{
		const Celestial::StarSystem halcyon = AiTests::Halcyon();
		return Celestial::EvaluateBody(halcyon, static_cast<std::size_t>(halcyon.Find(body)), time)
		    .metres;
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

	// The cruise's refusal, from the step the command is applied in.
	Sim::Reason Refusal(const Ai::Cruise &command)
	{
		Events<Ai::CruiseRefused>();
		world.Commands().Push(command);
		Run(1);
		const std::vector<Ai::CruiseRefused> refused = Events<Ai::CruiseRefused>();
		REQUIRE(refused.size() == 1);
		CHECK(refused[0].ship == command.ship);
		CHECK(refused[0].body == command.body);
		return refused[0].reason;
	}
};

const Celestial::Body &BodyOf(std::string_view name)
{
	static const Celestial::StarSystem halcyon = AiTests::Halcyon();
	return halcyon.bodies[static_cast<std::size_t>(halcyon.Find(name))];
}

// The way a transform's nose (-Z) points.
glm::dvec3 Nose(const Sim::Transform &transform)
{
	return transform.orientation * glm::dvec3{0.0, 0.0, -1.0};
}
} // namespace

TEST_CASE("Cruise: a drive's time rises as the distance's fourth root")
{
	CHECK(Ai::CruiseSeconds(kDrive, 1.0e8) == doctest::Approx(40.0));
	CHECK(Ai::CruiseSeconds(kDrive, 1.6e9) == doctest::Approx(80.0));
	CHECK(Ai::CruiseSeconds(kDrive, 0.0) == 0.0);
	// Off a station by its radius, off a body with gravity where it pulls at 1 m/s².
	CHECK(Ai::CruiseTravel::ArrivalDistance(BodyOf("Ostra Highport")) == doctest::Approx(4075.0));
	CHECK(Ai::CruiseTravel::ArrivalDistance(BodyOf("Pell")) == doctest::Approx(2.214688e6));
	CHECK(Ai::CruiseTravel::ArrivalDistance(BodyOf("Ostra")) == doctest::Approx(1.996498e7));
}

TEST_CASE("Cruise: a ship crosses from Highport to Pell out of physics, and arrives at rest off it")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	fixture.Run(5);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 1);
	fixture.Events<Ai::CruiseStarted>();

	const std::uint64_t departure = fixture.world.Clock().Step();
	fixture.world.Commands().Push(Ai::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	const std::vector<Ai::CruiseStarted> started = fixture.Events<Ai::CruiseStarted>();
	REQUIRE(started.size() == 1);
	CHECK(started[0].ship == ship);
	CHECK(started[0].body == "Pell");
	// The time its drive takes over its course, to within the step it is rounded up to and what the
	// last round of the solve moved the course by.
	CHECK(started[0].metres > 3.6e8);
	CHECK(std::abs(started[0].seconds - Ai::CruiseSeconds(kDrive, started[0].metres)) < 2.0 * kStep);
	const std::uint64_t arrival = started[0].arrivalStep;
	CHECK(arrival == departure + static_cast<std::uint64_t>(std::llround(started[0].seconds / kStep)));

	// Out of physics and its frame, and still in its system, placed along its course.
	const Ai::Cruising *cruising = registry.try_get<Ai::Cruising>(ship);
	REQUIRE(cruising);
	CHECK(cruising->target == "Pell");
	CHECK(cruising->frame == "Pell");
	CHECK(cruising->carrier == "Ostra");
	CHECK(cruising->departureStep == departure);
	CHECK(!registry.any_of<Physics::Body, Physics::Velocity, Physics::LocalFrame>(ship));
	CHECK(fixture.physics.BodyCount() == 0);
	CHECK(registry.get<Sim::InSystem>(ship).system == kHalcyon);

	// Clear of Ostra and Pell at every step, and closing on Pell, slowly at first.
	double nearestOstra = std::numeric_limits<double>::infinity();
	double firstSecond = 0.0;
	const glm::dvec3 setOut = registry.get<Sim::Transform>(ship).position -
	                          Fixture::Where("Ostra Highport", fixture.world.Clock().Now());
	while (fixture.world.Clock().Step() + 1 < arrival)
	{
		fixture.Run(1);
		const double now = fixture.world.Clock().Now();
		const glm::dvec3 at = registry.get<Sim::Transform>(ship).position;
		nearestOstra = std::min(nearestOstra, glm::distance(at, Fixture::Where("Ostra", now)));
		CHECK(glm::distance(at, Fixture::Where("Pell", now)) > BodyOf("Pell").radius);
		if (fixture.world.Clock().Step() == departure + 61)
			firstSecond = glm::length(at - Fixture::Where("Ostra Highport", now) - setOut);
	}
	MESSAGE(
	    "Highport to Pell: " << started[0].metres / 1000.0 << " km in " << started[0].seconds << " s, "
	                         << firstSecond << " m out in the first second, nearest Ostra "
	                         << (nearestOstra - BodyOf("Ostra").radius) / 1000.0 << " km up");
	CHECK(nearestOstra > BodyOf("Ostra").radius);
	CHECK(firstSecond > 0.0);
	CHECK(firstSecond < 20000.0);
	CHECK(registry.all_of<Ai::Cruising>(ship));
	CHECK(fixture.Events<Ai::CruiseEnded>().empty());

	// The step that ends the cruise brings it out, facing Pell, at rest in its frame, its body back.
	fixture.Run(1);
	CHECK(fixture.world.Clock().Step() == arrival);
	const std::vector<Ai::CruiseEnded> ended = fixture.Events<Ai::CruiseEnded>();
	REQUIRE(ended.size() == 1);
	CHECK(ended[0].ship == ship);
	CHECK(ended[0].body == "Pell");
	CHECK(!registry.all_of<Ai::Cruising>(ship));
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Pell");
	CHECK(registry.get<Physics::Velocity>(ship) == Physics::Velocity{});
	const Sim::Transform arrived = registry.get<Sim::Transform>(ship);
	const glm::dvec3 pell = Fixture::Where("Pell", fixture.world.Clock().Now());
	CHECK(
	    glm::distance(arrived.position, pell) ==
	    doctest::Approx(Ai::CruiseTravel::ArrivalDistance(BodyOf("Pell"))).epsilon(1.0e-9));
	CHECK(glm::dot(Nose(arrived), glm::normalize(pell - arrived.position)) > 0.999999);

	// Physics makes its body again in the next step, in Pell's frame, where it is all but still.
	fixture.Run(1);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 1);
	CHECK(fixture.physics.FrameCount(kHalcyon) == 1);
	CHECK(glm::length(registry.get<Physics::Velocity>(ship).linear) < 0.1f);

	// And back to the gate, which keeps station by Highport, in whose frame it arrives.
	fixture.world.Commands().Push(Ai::Cruise{{}, ship, "Halcyon Gate"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Ai::Cruising>(ship));
	CHECK(registry.get<Ai::Cruising>(ship).frame == "Ostra Highport");
	fixture.Run(registry.get<Ai::Cruising>(ship).arrivalStep - fixture.world.Clock().Step());
	CHECK(!registry.all_of<Ai::Cruising>(ship));
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Ostra Highport");
	// To the millimetre: a place 1.5 x 10^11 m from the star is held to about 30 µm.
	CHECK(
	    std::abs(
	        glm::distance(
	            registry.get<Sim::Transform>(ship).position,
	            Fixture::Where("Halcyon Gate", fixture.world.Clock().Now())) -
	        Ai::CruiseTravel::ArrivalDistance(BodyOf("Halcyon Gate"))) < 0.001);
}

TEST_CASE("Cruise: a course goes round the planet between a ship and its target")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	// In Ostra's frame, three of its radii out on the far side from Pell.
	const glm::dvec3 away = glm::normalize(Fixture::Where("Ostra", 0.0) - Fixture::Where("Pell", 0.0));
	const entt::entity ship = fixture.Ship("Ostra", away * 2.0e7);
	fixture.world.Commands().Push(Ai::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	const Ai::Cruising *cruising = registry.try_get<Ai::Cruising>(ship);
	REQUIRE(cruising);
	CHECK(cruising->carrier == "Ostra");
	CHECK(cruising->course.size() > 2);
	const std::size_t cruisingPoints = cruising->course.size();

	// Ostra carries the course, so its clearance holds at every step.
	double nearest = std::numeric_limits<double>::infinity();
	const std::uint64_t arrival = cruising->arrivalStep;
	while (fixture.world.Clock().Step() < arrival)
	{
		fixture.Run(1);
		nearest = std::min(
		    nearest, glm::distance(
		                 registry.get<Sim::Transform>(ship).position,
		                 Fixture::Where("Ostra", fixture.world.Clock().Now())));
	}
	MESSAGE(
	    "Round Ostra to Pell: " << cruisingPoints << " points, "
	                            << (nearest - BodyOf("Ostra").radius) / 1000.0 << " km up at nearest");
	CHECK(nearest > BodyOf("Ostra").radius + Ai::CruiseTravel::kCourseClearance - 1.0);
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Pell");
}

TEST_CASE("Cruise: off a planet where its pull is one its assist holds against")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	// From Pell: Highport, in low orbit, is already where a cruise to Ostra would arrive.
	const entt::entity ship = fixture.Ship("Pell", {0.0, 3.0e6, 0.0});
	registry.emplace<Physics::Thrusters>(ship);
	fixture.world.Commands().Push(Ai::Cruise{{}, ship, "Ostra"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Ai::Cruising>(ship));
	fixture.Run(registry.get<Ai::Cruising>(ship).arrivalStep - fixture.world.Clock().Step());
	REQUIRE(registry.get<Physics::LocalFrame>(ship).body == "Ostra");
	const glm::dvec3 arrived = registry.get<Sim::Transform>(ship).position -
	                           Fixture::Where("Ostra", fixture.world.Clock().Now());
	CHECK(glm::length(arrived) == doctest::Approx(1.996498e7));

	// A minute later, held by the assist against Ostra's 1 m/s², it has hardly moved in its frame.
	fixture.Run(3600);
	const glm::dvec3 held = registry.get<Sim::Transform>(ship).position -
	                        Fixture::Where("Ostra", fixture.world.Clock().Now());
	MESSAGE("Off Ostra, a minute after arriving: " << glm::distance(held, arrived) << " m from it");
	CHECK(glm::distance(held, arrived) < 5.0);
}

TEST_CASE("Cruise: to the star, a system's breadth, carried by nothing that moves")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 3000.0, 0.0});
	fixture.world.Commands().Push(Ai::Cruise{{}, ship, "Halcyon"});
	fixture.Run(1);
	const Ai::Cruising *cruising = registry.try_get<Ai::Cruising>(ship);
	REQUIRE(cruising);
	CHECK(cruising->carrier == "Halcyon");
	// A few minutes for a hundred and fifty million kilometres.
	const double seconds = static_cast<double>(cruising->arrivalStep - cruising->departureStep) * kStep;
	MESSAGE("Highport to Halcyon: " << seconds << " s");
	CHECK(seconds > 200.0);
	CHECK(seconds < 300.0);
	fixture.Run(cruising->arrivalStep - fixture.world.Clock().Step());
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Halcyon");
	CHECK(
	    glm::length(registry.get<Sim::Transform>(ship).position) ==
	    doctest::Approx(Ai::CruiseTravel::ArrivalDistance(BodyOf("Halcyon"))).epsilon(1.0e-9));
}

TEST_CASE("Cruise: refused, with its reason")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	const auto reason = [](Ai::Reason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{ return Sim::MakeReason(reason, std::move(arguments)); };
	const auto identifier = [](std::string name)
	{ return Sim::ReasonArgument::Identifier(std::move(name)); };

	CHECK(fixture.Refusal({{}, entt::entity{999}, "Pell"}) == reason(Ai::Reason::NoSuchEntity));
	CHECK(
	    fixture.Refusal({{}, ship, "Corva"}) ==
	    reason(Ai::Reason::NoSuchBody, {identifier("Halcyon"), identifier("Corva")}));

	// Commanded by a player, from another.
	const Sim::Controller player{Sim::ControllerKind::Player, 0};
	registry.emplace<Sim::ControlledBy>(ship, player);
	CHECK(
	    fixture.Refusal({{Sim::ControllerKind::Player, 1}, ship, "Pell"}) ==
	    reason(Ai::Reason::NotController));

	// Within a station's reach, so docking stays in physics.
	const entt::entity docking = fixture.Ship("Ostra Highport", {0.0, 0.0, 1500.0});
	CHECK(
	    fixture.Refusal({{}, docking, "Pell"}) ==
	    reason(Ai::Reason::NearStation, {identifier("Ostra Highport")}));

	// Without a drive, and held on rails.
	registry.remove<Ai::CruiseDrive>(docking);
	registry.get<Sim::Transform>(docking).position += glm::dvec3{0.0, 0.0, 2000.0};
	CHECK(fixture.Refusal({{}, docking, "Pell"}) == reason(Ai::Reason::CannotCruise));
	registry.emplace<Ai::CruiseDrive>(docking, kDrive);
	registry.emplace<Physics::OnRails>(docking, Physics::OnRails{"Ostra Highport"});
	CHECK(fixture.Refusal({{}, docking, "Pell"}) == reason(Ai::Reason::CannotCruise));

	// Already where a cruise would bring it.
	const entt::entity near = fixture.Ship("Pell", {0.0, 2.0e6, 0.0});
	CHECK(
	    fixture.Refusal({{}, near, "Pell"}) == reason(Ai::Reason::AlreadyThere, {identifier("Pell")}));

	// Cruising, a second cruise and a lane are refused.
	fixture.world.Commands().Push(Ai::Cruise{player, ship, "Pell"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Ai::Cruising>(ship));
	CHECK(fixture.Refusal({player, ship, "Halcyon"}) == reason(Ai::Reason::AlreadyCruising));
	fixture.Events<Ai::LaneRefused>();
	fixture.world.Commands().Push(Ai::EnterLane{ship, "Halcyon Gate"});
	fixture.Run(1);
	const std::vector<Ai::LaneRefused> lane = fixture.Events<Ai::LaneRefused>();
	REQUIRE(lane.size() == 1);
	CHECK(lane[0].reason == reason(Ai::Reason::AlreadyCruising));
	CHECK(registry.all_of<Ai::Cruising>(ship));
}

TEST_CASE("Cruise: a world saved mid-cruise arrives as the one saved does")
{
	Fixture fixture;
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	fixture.Run(10);
	fixture.world.Commands().Push(Ai::Cruise{{}, ship, "Pell"});
	fixture.Run(1000);
	const std::vector<std::byte> midway = fixture.world.Save();
	const std::uint64_t arrival = fixture.world.Registry().get<Ai::Cruising>(ship).arrivalStep;
	fixture.Run(arrival - fixture.world.Clock().Step() + 30);
	const std::uint64_t arrived = fixture.world.StateHash();

	Fixture loaded;
	loaded.world.Load(midway);
	REQUIRE(loaded.world.Registry().all_of<Ai::Cruising>(ship));
	loaded.Run(arrival - loaded.world.Clock().Step() + 30);
	CHECK(loaded.world.Registry().get<Physics::LocalFrame>(ship).body == "Pell");
	CHECK(loaded.world.StateHash() == arrived);
}
