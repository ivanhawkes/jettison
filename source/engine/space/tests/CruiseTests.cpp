#include "Systems.h"

#include <jettison/ai/Types.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Lanes.h>
#include <jettison/space/StarSystemFrame.h>

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
const std::uint64_t kHalcyon = Sim::FrameId("Halcyon");
// A shuttle's drive (defs/hulls/shuttle.yaml).
constexpr Space::CruiseDrive kDrive{40.0f, 1.0e8, 0.25f};

// A world as a game's start-up makes one: physics, the lanes, then cruising, given the sample
// galaxy's systems.
struct Fixture
{
	Sim::SimWorld world{kStep, 7};
	Physics::PhysicsWorld &physics;
	Space::LaneTravel &lanes;
	Space::CruiseTravel &cruises;


	Fixture()
	    : physics{Setup(world)}, lanes{Space::LaneTravel::Attach(world, physics)},
	      cruises{Space::CruiseTravel::Attach(world, physics)}
	{
		Physics::ShapeDefinition hull;
		hull.halfExtents = {3.2f, 0.9f, 4.9f};
		physics.DefineShape("hull", hull);
		for (const Celestial::StarSystem &system :
		     {AiTests::Halcyon(), AiTests::Vesper(), AiTests::Marrow()})
		{
			Space::DefineStarSystem(physics, system);
			lanes.DefineStarSystem(system);
			cruises.DefineStarSystem(system);
		}
		lanes.SetGalaxy(AiTests::Sample());
	}


	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
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
		registry.emplace<Sim::InFrame>(ship, kHalcyon);
		registry.emplace<Physics::LocalFrame>(ship, std::string{frame});
		Physics::Body body;
		body.kind = Physics::BodyKind::Dynamic;
		body.shape = "hull";
		body.mass = 20000.0f;
		registry.emplace<Physics::Body>(ship, body);
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::FlightControls>(ship);
		registry.emplace<Space::CruiseDrive>(ship, kDrive);
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
	Sim::Reason Refusal(const Space::Cruise &command)
	{
		Events<Space::CruiseRefused>();
		world.Commands().Push(command);
		Run(1);
		const std::vector<Space::CruiseRefused> refused = Events<Space::CruiseRefused>();
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
	CHECK(Space::CruiseSeconds(kDrive, 1.0e8) == doctest::Approx(40.0));
	CHECK(Space::CruiseSeconds(kDrive, 1.6e9) == doctest::Approx(80.0));
	CHECK(Space::CruiseSeconds(kDrive, 0.0) == 0.0);
	// Off a station by its radius, off a body with gravity where it pulls at 1 m/s².
	CHECK(Space::CruiseTravel::ArrivalDistance(BodyOf("Ostra Highport")) == doctest::Approx(4075.0));
	CHECK(Space::CruiseTravel::ArrivalDistance(BodyOf("Pell")) == doctest::Approx(2.214688e6));
	CHECK(Space::CruiseTravel::ArrivalDistance(BodyOf("Ostra")) == doctest::Approx(1.996498e7));
}


TEST_CASE("Cruise: a drive of no time, no reference or an exponent outside (0, 1] is refused")
{
	CHECK_NOTHROW(Space::CheckCruiseDrive("shuttle", kDrive));
	CHECK_NOTHROW(Space::CheckCruiseDrive("linear", {40.0f, 1.0e8, 1.0f}));
	for (const Space::CruiseDrive drive :
	     {Space::CruiseDrive{0.0f, 1.0e8, 0.25f}, Space::CruiseDrive{40.0f, 0.0, 0.25f},
	      Space::CruiseDrive{40.0f, 1.0e8, 0.0f}, Space::CruiseDrive{40.0f, 1.0e8, 1.5f},
	      Space::CruiseDrive{std::numeric_limits<float>::infinity(), 1.0e8, 0.25f}})
		CHECK_THROWS_AS(Space::CheckCruiseDrive("bad", drive), Serialise::FormatError);
}


TEST_CASE("Cruise: a ship crosses from Highport to Pell out of physics, and arrives at rest off it")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	fixture.Run(5);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 1);
	fixture.Events<Space::CruiseStarted>();

	const std::uint64_t departure = fixture.world.Clock().Step();
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	const std::vector<Space::CruiseStarted> started = fixture.Events<Space::CruiseStarted>();
	REQUIRE(started.size() == 1);
	CHECK(started[0].ship == ship);
	CHECK(started[0].body == "Pell");
	// The time its drive takes over its course, to within the step it is rounded up to and what the
	// last round of the solve moved the course by.
	CHECK(started[0].metres > 3.6e8);
	CHECK(std::abs(started[0].seconds - Space::CruiseSeconds(kDrive, started[0].metres)) < 2.0 * kStep);
	const std::uint64_t arrival = started[0].arrivalStep;
	CHECK(arrival == departure + static_cast<std::uint64_t>(std::llround(started[0].seconds / kStep)));

	// Out of physics and its frame, and still in its system, placed along its course.
	const Space::Cruising *cruising = registry.try_get<Space::Cruising>(ship);
	REQUIRE(cruising);
	CHECK(cruising->target == "Pell");
	CHECK(cruising->frame == "Pell");
	CHECK(cruising->carrier == "Ostra");
	CHECK(cruising->departureStep == departure);
	CHECK(!registry.any_of<Physics::Body, Physics::Velocity, Physics::LocalFrame>(ship));
	CHECK(fixture.physics.BodyCount() == 0);
	CHECK(registry.get<Sim::InFrame>(ship).frame == kHalcyon);

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
	CHECK(registry.all_of<Space::Cruising>(ship));
	CHECK(fixture.Events<Space::CruiseEnded>().empty());

	// The step that ends the cruise brings it out, facing Pell, at rest in its frame, its body back.
	fixture.Run(1);
	CHECK(fixture.world.Clock().Step() == arrival);
	const std::vector<Space::CruiseEnded> ended = fixture.Events<Space::CruiseEnded>();
	REQUIRE(ended.size() == 1);
	CHECK(ended[0].ship == ship);
	CHECK(ended[0].body == "Pell");
	CHECK(!registry.all_of<Space::Cruising>(ship));
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Pell");
	CHECK(registry.get<Physics::Velocity>(ship) == Physics::Velocity{});
	const Sim::Transform arrived = registry.get<Sim::Transform>(ship);
	const glm::dvec3 pell = Fixture::Where("Pell", fixture.world.Clock().Now());
	CHECK(
	    glm::distance(arrived.position, pell) ==
	    doctest::Approx(Space::CruiseTravel::ArrivalDistance(BodyOf("Pell"))).epsilon(1.0e-9));
	CHECK(glm::dot(Nose(arrived), glm::normalize(pell - arrived.position)) > 0.999999);

	// Physics makes its body again in the next step, in Pell's frame, where it is all but still.
	fixture.Run(1);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 1);
	CHECK(fixture.physics.FrameCount(kHalcyon) == 1);
	CHECK(glm::length(registry.get<Physics::Velocity>(ship).linear) < 0.1f);

	// And back to the gate, which keeps station by Highport, in whose frame it arrives.
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Halcyon Gate"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Space::Cruising>(ship));
	CHECK(registry.get<Space::Cruising>(ship).frame == "Ostra Highport");
	fixture.Run(registry.get<Space::Cruising>(ship).arrivalStep - fixture.world.Clock().Step());
	CHECK(!registry.all_of<Space::Cruising>(ship));
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Ostra Highport");
	// To the millimetre: a place 1.5 x 10^11 m from the star is held to about 30 µm.
	CHECK(
	    std::abs(
	        glm::distance(
	            registry.get<Sim::Transform>(ship).position,
	            Fixture::Where("Halcyon Gate", fixture.world.Clock().Now())) -
	        Space::CruiseTravel::ArrivalDistance(BodyOf("Halcyon Gate"))) < 0.001);
}


TEST_CASE("Cruise: ships that arrive together at one body come out clear of each other")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	// Two that set out a few metres apart, and one that has been waiting where they would come out.
	const entt::entity first = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	const entt::entity second = fixture.Ship("Ostra Highport", {4.0, 0.0, 3000.0});
	fixture.Run(5);
	fixture.world.Commands().Push(Space::Cruise{{}, first, "Pell"});
	fixture.world.Commands().Push(Space::Cruise{{}, second, "Pell"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Space::Cruising>(first));
	REQUIRE(registry.all_of<Space::Cruising>(second));
	const std::uint64_t arrival = std::max(
	    registry.get<Space::Cruising>(first).arrivalStep,
	    registry.get<Space::Cruising>(second).arrivalStep);
	fixture.Run(arrival - fixture.world.Clock().Step());
	REQUIRE(!registry.all_of<Space::Cruising>(first));
	REQUIRE(!registry.all_of<Space::Cruising>(second));

	const double hull = fixture.physics.ShapeRadius("hull");
	const glm::dvec3 firstAt = registry.get<Sim::Transform>(first).position;
	const glm::dvec3 secondAt = registry.get<Sim::Transform>(second).position;
	const double apart = glm::distance(firstAt, secondAt) - 2.0 * hull;
	MESSAGE("Arrived " << apart << " m between hulls");
	CHECK(apart >= Space::LaneTravel::kArrivalClearance);
	// Not further than the first ring, a hull's breadth and the clearance out, and the few metres
	// the two courses differ by.
	CHECK(glm::distance(firstAt, secondAt) <= 2.0 * hull + Space::LaneTravel::kArrivalClearance + 10.0);
	const glm::dvec3 pell = Fixture::Where("Pell", fixture.world.Clock().Now());
	for (const glm::dvec3 &at : {firstAt, secondAt})
		CHECK(
		    std::abs(glm::distance(at, pell) - Space::CruiseTravel::ArrivalDistance(BodyOf("Pell"))) <
		    2.0 * hull + Space::LaneTravel::kArrivalClearance);
}


TEST_CASE("Cruise: a course goes round the planet between a ship and its target")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	// In Ostra's frame, three of its radii out on the far side from Pell.
	const glm::dvec3 away = glm::normalize(Fixture::Where("Ostra", 0.0) - Fixture::Where("Pell", 0.0));
	const entt::entity ship = fixture.Ship("Ostra", away * 2.0e7);
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	const Space::Cruising *cruising = registry.try_get<Space::Cruising>(ship);
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
	CHECK(nearest > BodyOf("Ostra").radius + Space::CruiseTravel::kCourseClearance - 1.0);
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Pell");
}


TEST_CASE("Cruise: off a planet where its pull is one its assist holds against")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	// From Pell: Highport, in low orbit, is already where a cruise to Ostra would arrive.
	const entt::entity ship = fixture.Ship("Pell", {0.0, 3.0e6, 0.0});
	registry.emplace<Physics::Handling>(ship);
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Ostra"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Space::Cruising>(ship));
	fixture.Run(registry.get<Space::Cruising>(ship).arrivalStep - fixture.world.Clock().Step());
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
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Halcyon"});
	fixture.Run(1);
	const Space::Cruising *cruising = registry.try_get<Space::Cruising>(ship);
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
	    doctest::Approx(Space::CruiseTravel::ArrivalDistance(BodyOf("Halcyon"))).epsilon(1.0e-9));
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
	registry.remove<Space::CruiseDrive>(docking);
	registry.get<Sim::Transform>(docking).position += glm::dvec3{0.0, 0.0, 2000.0};
	CHECK(fixture.Refusal({{}, docking, "Pell"}) == reason(Ai::Reason::CannotCruise));
	registry.emplace<Space::CruiseDrive>(docking, kDrive);
	registry.emplace<Physics::OnRails>(docking, Physics::OnRails{"Ostra Highport"});
	CHECK(fixture.Refusal({{}, docking, "Pell"}) == reason(Ai::Reason::CannotCruise));

	// Already where a cruise would bring it.
	const entt::entity near = fixture.Ship("Pell", {0.0, 2.0e6, 0.0});
	CHECK(
	    fixture.Refusal({{}, near, "Pell"}) == reason(Ai::Reason::AlreadyThere, {identifier("Pell")}));

	// Cruising, a second cruise and a lane are refused.
	fixture.world.Commands().Push(Space::Cruise{player, ship, "Pell"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Space::Cruising>(ship));
	CHECK(fixture.Refusal({player, ship, "Halcyon"}) == reason(Ai::Reason::AlreadyCruising));
	fixture.Events<Space::LaneRefused>();
	fixture.world.Commands().Push(Space::EnterLane{{}, ship, "Halcyon Gate"});
	fixture.Run(1);
	const std::vector<Space::LaneRefused> lane = fixture.Events<Space::LaneRefused>();
	REQUIRE(lane.size() == 1);
	CHECK(lane[0].reason == reason(Ai::Reason::AlreadyCruising));
	CHECK(registry.all_of<Space::Cruising>(ship));
}


TEST_CASE("Cruise: a world saved mid-cruise arrives as the one saved does")
{
	Fixture fixture;
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	fixture.Run(10);
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	fixture.Run(1000);
	const std::vector<std::byte> midway = fixture.world.Save();
	const std::uint64_t arrival = fixture.world.Registry().get<Space::Cruising>(ship).arrivalStep;
	fixture.Run(arrival - fixture.world.Clock().Step() + 30);
	const std::uint64_t arrived = fixture.world.StateHash();

	Fixture loaded;
	loaded.world.Load(midway);
	REQUIRE(loaded.world.Registry().all_of<Space::Cruising>(ship));
	loaded.Run(arrival - loaded.world.Clock().Step() + 30);
	CHECK(loaded.world.Registry().get<Physics::LocalFrame>(ship).body == "Pell");
	CHECK(loaded.world.StateHash() == arrived);
}
