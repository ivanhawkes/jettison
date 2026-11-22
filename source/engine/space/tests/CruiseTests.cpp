#include "Systems.h"

#include <jettison/ai/Steering.h>
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
#include <cmath>
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
constexpr Space::CruiseDrive kDrive{40.0f, 1.0e8, 0.25f, {}};
constexpr double kStandardGravity = 9.80665;
// A profile far past any crew's, so a test's cruise from Highport to Pell is minutes, not hours:
// boosting 10 s at each end, sustained between.
constexpr Space::CruiseProfile kHard{20000.0f, 20.0f, 5000.0f};
// The same thrusting for a quarter of the cruise, coasting for the rest.
constexpr Space::CruiseProfile kCoasting{20000.0f, 20.0f, 5000.0f, 0.25f};

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


TEST_CASE("Cruise: a profile's time and what it covers are its closed form")
{
	// 2 g for 4 hours, 2 at each end, and 1 g between.
	const Space::CruiseProfile manned{
	    static_cast<float>(2.0 * kStandardGravity), 14400.0f, static_cast<float>(kStandardGravity)};
	const Space::CruiseDrive drive{.profile = manned};
	const auto boost = double{manned.boostAcceleration};
	const auto sustained = double{manned.sustainedAcceleration};

	// Short of the boost's distance it is at the boost throughout: 2 √(d / a).
	CHECK(Space::CruiseSeconds(drive, 1.0e6) == doctest::Approx(2.0 * std::sqrt(1.0e6 / boost)));
	CHECK(Space::CruiseSeconds(drive, 0.0) == 0.0);

	// Two hours at the boost and one at the sustained rate each way.
	const double boosted = boost * 7200.0 * 7200.0 / 2.0;
	const double speed = boost * 7200.0;
	const double half = boosted + (speed * 3600.0) + (sustained * 3600.0 * 3600.0 / 2.0);
	CHECK(Space::CruiseSeconds(drive, 2.0 * half) == doctest::Approx(21600.0).epsilon(1.0e-12));
	CHECK(Space::CruiseCovered(manned, 2.0 * half, 0.0) == 0.0);
	CHECK(Space::CruiseCovered(manned, 2.0 * half, 3600.0) == doctest::Approx(boost * 3600.0 * 1800.0));
	CHECK(Space::CruiseCovered(manned, 2.0 * half, 7200.0) == doctest::Approx(boosted));
	CHECK(Space::CruiseCovered(manned, 2.0 * half, 10800.0) == doctest::Approx(half));
	CHECK(
	    Space::CruiseCovered(manned, 2.0 * half, 21600.0 - 3600.0) ==
	    doctest::Approx((2.0 * half) - (boost * 3600.0 * 1800.0)));
	CHECK(Space::CruiseCovered(manned, 2.0 * half, 21600.0) == 2.0 * half);
	CHECK(Space::CruiseCovered(manned, 2.0 * half, 1.0e9) == 2.0 * half);

	// Unmanned, 12 g throughout.
	const auto twelve = static_cast<float>(12.0 * kStandardGravity);
	const Space::CruiseDrive unmanned{.profile = Space::CruiseProfile{twelve, 0.0f, twelve}};
	CHECK(
	    Space::CruiseSeconds(unmanned, 4.0e11) ==
	    doctest::Approx(2.0 * std::sqrt(4.0e11 / double{twelve})));
	CHECK_NOTHROW(Space::CheckCruiseDrive("manned", drive));
	CHECK_NOTHROW(Space::CheckCruiseDrive("unmanned", unmanned));
	CHECK_NOTHROW(Space::CheckCruiseDrive("coasting", {.profile = Space::CruiseProfile{20.0f, 60.0f}}));
}


TEST_CASE("Cruise: a profile thrusting for a share of the cruise coasts between, in closed form")
{
	// 1 g throughout over Ore to Brine's 396 million km (Manifest's W21 plan): a share f reaches
	// s = √(f / (2 − f)) of the full thrust's speed, so the cruise takes (1 + s²) / 2s times as long.
	const auto g = static_cast<float>(kStandardGravity);
	const double metres = 3.96e11;
	const Space::CruiseProfile full{g, 0.0f, g};
	const double fullSeconds = Space::CruiseSeconds({.profile = full}, metres);
	for (const float share : {0.5f, 0.25f, 0.1f})
	{
		Space::CruiseProfile coasting = full;
		coasting.thrustShare = share;
		const double reached = std::sqrt(double{share} / (2.0 - double{share}));
		const double seconds = Space::CruiseSeconds({.profile = coasting}, metres);
		CHECK(
		    seconds == doctest::Approx(fullSeconds * (1.0 + (reached * reached)) / (2.0 * reached))
		                   .epsilon(1.0e-12));
		const Space::CruiseThrustSeconds thrust = Space::CruiseThrust(coasting, metres);
		CHECK(thrust.boost == 0.0);
		CHECK(2.0 * thrust.sustained / seconds == doctest::Approx(double{share}).epsilon(1.0e-12));

		// Thrusting from rest, then coasting at the speed reached to the turn and on to the burn in.
		const double thrusted = double{g} * thrust.sustained * thrust.sustained / 2.0;
		const double top = double{g} * thrust.sustained;
		CHECK(Space::CruiseCovered(coasting, metres, thrust.sustained) == doctest::Approx(thrusted));
		CHECK(
		    Space::CruiseCovered(coasting, metres, seconds / 2.0) ==
		    doctest::Approx(metres / 2.0).epsilon(1.0e-12));
		CHECK(
		    Space::CruiseCovered(coasting, metres, (seconds / 2.0) + 3600.0) ==
		    doctest::Approx((metres / 2.0) + (top * 3600.0)).epsilon(1.0e-12));
		CHECK(
		    Space::CruiseCovered(coasting, metres, seconds - thrust.sustained) ==
		    doctest::Approx(metres - thrusted));
		CHECK(Space::CruiseCovered(coasting, metres, seconds) == metres);
		CHECK_NOTHROW(Space::CheckCruiseDrive("coasting", {.profile = coasting}));
	}
	Space::CruiseProfile half = full;
	half.thrustShare = 0.5f;
	MESSAGE(
	    "Ore to Brine at 1 g: " << fullSeconds / 86400.0 << " days thrusting throughout, "
	                            << Space::CruiseSeconds({.profile = half}, metres) / 86400.0
	                            << " at half the time");
	CHECK(fullSeconds / 86400.0 == doctest::Approx(4.65).epsilon(0.001));
	CHECK(
	    Space::CruiseSeconds({.profile = half}, metres) / 86400.0 ==
	    doctest::Approx(5.37).epsilon(0.001));

	// 2 g for 4 hours, 2 at each end, and 1 g between: a quarter of a long cruise thrusts past the
	// boost, a quarter of a short one only on it.
	Space::CruiseProfile manned{
	    static_cast<float>(2.0 * kStandardGravity), 14400.0f, static_cast<float>(kStandardGravity),
	    0.25f};
	for (const double distance : {metres, 1.0e8})
	{
		const double seconds = Space::CruiseSeconds({.profile = manned}, distance);
		const Space::CruiseThrustSeconds thrust = Space::CruiseThrust(manned, distance);
		CHECK(
		    2.0 * (thrust.boost + thrust.sustained) / seconds ==
		    doctest::Approx(0.25).epsilon(1.0e-12));
		const auto boost = double{manned.boostAcceleration};
		const double boosted = boost * thrust.boost * thrust.boost / 2.0;
		const auto sustained = double{manned.sustainedAcceleration};
		const double thrusted = boosted + (boost * thrust.boost * thrust.sustained) +
		                        (sustained * thrust.sustained * thrust.sustained / 2.0);
		CHECK(Space::CruiseCovered(manned, distance, thrust.boost) == doctest::Approx(boosted));
		CHECK(
		    Space::CruiseCovered(manned, distance, thrust.boost + thrust.sustained) ==
		    doctest::Approx(thrusted));
		CHECK(
		    Space::CruiseCovered(manned, distance, seconds / 2.0) ==
		    doctest::Approx(distance / 2.0).epsilon(1.0e-12));
	}
	CHECK(Space::CruiseThrust(manned, metres).boost == 7200.0);
	CHECK(Space::CruiseThrust(manned, metres).sustained > 0.0);
	CHECK(Space::CruiseThrust(manned, 1.0e8).boost < 7200.0);
	CHECK(Space::CruiseThrust(manned, 1.0e8).sustained == 0.0);
	CHECK(Space::CruiseThrust(manned, 0.0) == Space::CruiseThrustSeconds{});

	// A share of 1 is none, to the bit, and thrusts to the midpoint.
	const Space::CruiseProfile throughout{
	    manned.boostAcceleration, manned.boostSeconds, manned.sustainedAcceleration};
	manned.thrustShare = 1.0f;
	CHECK(
	    Space::CruiseSeconds({.profile = manned}, metres) ==
	    Space::CruiseSeconds({.profile = throughout}, metres));
	for (const double at : {3600.0, 7200.0, 1.0e5, 3.0e5})
		CHECK(Space::CruiseCovered(manned, metres, at) == Space::CruiseCovered(throughout, metres, at));
	const Space::CruiseThrustSeconds thrust = Space::CruiseThrust(throughout, metres);
	CHECK(
	    2.0 * (thrust.boost + thrust.sustained) ==
	    doctest::Approx(Space::CruiseSeconds({.profile = throughout}, metres)).epsilon(1.0e-12));
	CHECK_NOTHROW(Space::CheckCruiseDrive("throughout", {.profile = manned}));
}


TEST_CASE("Cruise: a profile of no boost, or one that is past it or never ends, is refused")
{
	const float infinity = std::numeric_limits<float>::infinity();
	for (const Space::CruiseProfile profile :
	     {Space::CruiseProfile{0.0f, 60.0f, 0.0f}, Space::CruiseProfile{infinity, 60.0f, 10.0f},
	      Space::CruiseProfile{20.0f, -1.0f, 10.0f}, Space::CruiseProfile{20.0f, infinity, 10.0f},
	      Space::CruiseProfile{20.0f, 60.0f, -1.0f}, Space::CruiseProfile{20.0f, 60.0f, 30.0f},
	      Space::CruiseProfile{20.0f, 0.0f, 0.0f}})
		CHECK_THROWS_AS(Space::CheckCruiseDrive("bad", {.profile = profile}), Serialise::FormatError);
	// A thrust share not above 0 and at most 1.
	for (const float share : {0.0f, -0.5f, 1.5f, infinity, std::numeric_limits<float>::quiet_NaN()})
		CHECK_THROWS_AS(
		    Space::CheckCruiseDrive(
		        "bad", {.profile = Space::CruiseProfile{20.0f, 60.0f, 10.0f, share}}),
		    Serialise::FormatError);
}


TEST_CASE("Cruise: a drive of no time, no reference or an exponent outside (0, 1] is refused")
{
	CHECK_NOTHROW(Space::CheckCruiseDrive("shuttle", kDrive));
	CHECK_NOTHROW(Space::CheckCruiseDrive("linear", {40.0f, 1.0e8, 1.0f, {}}));
	for (const Space::CruiseDrive drive :
	     {Space::CruiseDrive{0.0f, 1.0e8, 0.25f, {}}, Space::CruiseDrive{40.0f, 0.0, 0.25f, {}},
	      Space::CruiseDrive{40.0f, 1.0e8, 0.0f, {}}, Space::CruiseDrive{40.0f, 1.0e8, 1.5f, {}},
	      Space::CruiseDrive{std::numeric_limits<float>::infinity(), 1.0e8, 0.25f, {}}})
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


TEST_CASE(
    "Cruise: a ship within a station's reach is given the flight out of it, from where it cruises")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	Physics::ShapeDefinition hub;
	hub.halfExtents = {40.0f, 20.0f, 40.0f};
	fixture.physics.DefineShape("hub", hub);
	const entt::entity station = registry.create();
	registry.emplace<Sim::Transform>(
	    station, Sim::Transform{Fixture::Where("Ostra Highport", fixture.world.Clock().Now())});
	registry.emplace<Sim::InFrame>(station, kHalcyon);
	registry.emplace<Physics::OnRails>(station, Physics::OnRails{"Ostra Highport"});
	registry.emplace<Physics::Body>(station, Physics::Body{Physics::BodyKind::Kinematic, "hub"});
	const Celestial::StarSystem halcyon = AiTests::Halcyon();

	// Beyond Pell from the station, so the way out runs through the hub and goes round it.
	const glm::dvec3 toPell =
	    glm::normalize(Fixture::Where("Pell", 0.0) - Fixture::Where("Ostra Highport", 0.0));
	const entt::entity ship = fixture.Ship("Ostra Highport", -toPell * 200.0);
	const std::optional<Ai::Destination> flight =
	    Space::ClearOf(fixture.world, fixture.physics, halcyon, ship, station, "Pell");
	REQUIRE(flight);
	CHECK_FALSE(flight->waypoints.empty());
	const glm::dvec3 origin = fixture.physics.FrameOrigin(kHalcyon, "Ostra Highport", 0.0);
	const glm::dvec3 end = origin + flight->offset;
	const double reach = BodyOf("Ostra Highport").radius + Space::CruiseTravel::kStationReach;
	CHECK(
	    glm::distance(end, Fixture::Where("Ostra Highport", 0.0)) ==
	    doctest::Approx(reach + Space::kReachMargin));
	CHECK(glm::dot(end - Fixture::Where("Ostra Highport", 0.0), toPell) > 0.0);
	// Each waypoint is clear of the hub's sphere.
	for (const glm::dvec3 &waypoint : flight->waypoints)
		CHECK(
		    glm::distance(origin + waypoint, Fixture::Where("Ostra Highport", 0.0)) >
		    fixture.physics.ShapeRadius("hub"));

	// Where the flight ends, the cruise is accepted; outside the reach, there is no flight to take.
	registry.get<Sim::Transform>(ship).position = end;
	CHECK_FALSE(Space::ClearOf(fixture.world, fixture.physics, halcyon, ship, station, "Pell"));
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	CHECK(registry.all_of<Space::Cruising>(ship));

	// Nor for a body the system does not have, or a ship in no frame.
	const entt::entity other = fixture.Ship("Ostra Highport", {0.0, 0.0, 500.0});
	CHECK_FALSE(Space::ClearOf(fixture.world, fixture.physics, halcyon, other, station, "Corva"));
	registry.remove<Physics::LocalFrame>(other);
	CHECK_FALSE(Space::ClearOf(fixture.world, fixture.physics, halcyon, other, station, "Pell"));
}


// A game's system after physics, as Manifest's travel is, sees every Transform placed for the step's
// end; a station's frame moves with its planet by far more than the reach's margin in a step.
TEST_CASE("Cruise: after physics in a step, a station's reach is measured where physics placed it")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	Physics::ShapeDefinition hub;
	hub.halfExtents = {40.0f, 20.0f, 40.0f};
	fixture.physics.DefineShape("hub", hub);
	const entt::entity station = registry.create();
	registry.emplace<Sim::Transform>(
	    station, Sim::Transform{Fixture::Where("Ostra Highport", fixture.world.Clock().Now())});
	registry.emplace<Sim::InFrame>(station, kHalcyon);
	registry.emplace<Physics::OnRails>(station, Physics::OnRails{"Ostra Highport"});
	registry.emplace<Physics::Body>(station, Physics::Body{Physics::BodyKind::Kinematic, "hub"});
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 500.0});
	const Celestial::StarSystem halcyon = AiTests::Halcyon();
	const double reach = BodyOf("Ostra Highport").radius + Space::CruiseTravel::kStationReach;
	constexpr double kOutside = 10.0;

	bool checked = false;
	bool cleared = false;
	bool cruising = false;
	double moved = 0.0;
	fixture.world.RegisterSystem(
	    {"test.after_physics",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<
	         Sim::Transform, Sim::InFrame, Physics::OnRails, Physics::Body, Physics::LocalFrame,
	         Space::CruiseDrive>()
	         .Writing<
	             Sim::Transform, Space::Cruising, Physics::Body, Physics::LocalFrame, Physics::Velocity,
	             Physics::FlightControls>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [&](entt::registry &, double)
	     {
		     if (checked)
			     return;
		     checked = true;
		     // Just outside the reach of the station as placed, behind it as it goes.
		     const double now = fixture.world.Clock().Now();
		     const glm::dvec3 placed = registry.get<const Sim::Transform>(station).position;
		     moved = glm::distance(placed, Fixture::Where("Ostra Highport", now));
		     const glm::dvec3 going = glm::normalize(placed - Fixture::Where("Ostra Highport", now));
		     registry.get<Sim::Transform>(ship).position = placed - (going * (reach + kOutside));
		     cleared = !Space::ClearOf(fixture.world, fixture.physics, halcyon, ship, station, "Pell");
		     fixture.world.Apply(Space::Cruise{{}, ship, "Pell"});
		     cruising = registry.all_of<Space::Cruising>(ship);
	     }});
	fixture.Run(1);
	REQUIRE(checked);
	REQUIRE(fixture.physics.PlacedAt());
	CHECK(*fixture.physics.PlacedAt() == doctest::Approx(kStep));
	// Measured at the clock's Now, it would be inside by all but the margin.
	CHECK(moved > 10.0 * kOutside);
	CHECK(cleared);
	CHECK(cruising);
}


TEST_CASE(
    "Cruise: a steered ship gives up its destination and controls with it, a piloted one keeps them")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity steered = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	const entt::entity piloted = fixture.Ship("Ostra Highport", {0.0, 0.0, -3000.0});
	// What steering last demanded of one, and a pilot of the other.
	registry.emplace<Ai::Destination>(steered);
	const Physics::FlightControls demand{{0.0f, 0.0f, -0.6f}, {}, true, entt::null};
	registry.replace<Physics::FlightControls>(steered, demand);
	registry.replace<Physics::FlightControls>(piloted, demand);
	fixture.world.Commands().Push(Space::Cruise{{}, steered, "Pell"});
	fixture.world.Commands().Push(Space::Cruise{{}, piloted, "Pell"});
	fixture.Run(1);
	REQUIRE(registry.all_of<Space::Cruising>(steered));
	CHECK_FALSE(registry.all_of<Ai::Destination>(steered));
	CHECK(registry.get<Physics::FlightControls>(steered) == Physics::FlightControls{});
	CHECK(registry.get<Physics::FlightControls>(piloted) == demand);

	// And so it comes out: let go, taken against nothing.
	while (registry.all_of<Space::Cruising>(steered))
		fixture.Run(1);
	CHECK(registry.get<Physics::FlightControls>(steered) == Physics::FlightControls{});
}


TEST_CASE("Cruise: on a profile, a ship covers its course as the profile does, never past its boost")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	const Space::CruiseDrive drive{.profile = kHard};
	registry.replace<Space::CruiseDrive>(ship, drive);
	fixture.Run(5);
	fixture.Events<Space::CruiseStarted>();
	const std::uint64_t departure = fixture.world.Clock().Step();
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	const std::vector<Space::CruiseStarted> started = fixture.Events<Space::CruiseStarted>();
	REQUIRE(started.size() == 1);
	const Space::Cruising cruising = registry.get<Space::Cruising>(ship);
	REQUIRE(cruising.course.size() == 2);
	CHECK(cruising.profile == kHard);
	// Time enough for the course, and within a few steps of the least.
	const double least = Space::CruiseSeconds(drive, started[0].metres);
	CHECK(started[0].seconds >= least);
	CHECK(started[0].seconds < least + (4.0 * kStep));

	// Along a straight course from the carrier's point of view: no step accelerates past the boost,
	// and the boost and the sustained rate are both reached.
	const auto relative = [&]
	{
		return registry.get<Sim::Transform>(ship).position -
		       Fixture::Where("Ostra", fixture.world.Clock().Now());
	};
	std::vector<glm::dvec3> places{cruising.course.front(), relative()};
	double hardest = 0.0;
	double quarter = 0.0;
	const std::uint64_t quarterway = departure + ((started[0].arrivalStep - departure) / 4);
	while (fixture.world.Clock().Step() + 1 < started[0].arrivalStep)
	{
		fixture.Run(1);
		places.push_back(relative());
		const std::size_t last = places.size() - 1;
		const double acceleration =
		    glm::length(places[last] - (2.0 * places[last - 1]) + places[last - 2]) / (kStep * kStep);
		hardest = std::max(hardest, acceleration);
		if (fixture.world.Clock().Step() == quarterway)
			quarter = acceleration;
	}
	MESSAGE(
	    "Highport to Pell on a profile: " << started[0].metres / 1000.0 << " km in "
	                                      << started[0].seconds << " s, hardest " << hardest
	                                      << " m/s², a quarter in " << quarter << " m/s²");
	CHECK(hardest <= double{kHard.boostAcceleration} * 1.001);
	CHECK(hardest > double{kHard.boostAcceleration} * 0.99);
	CHECK(quarter == doctest::Approx(double{kHard.sustainedAcceleration}).epsilon(0.01));
	CHECK(glm::distance(places.back(), cruising.course.back()) < 10.0);

	fixture.Run(1);
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Pell");
	CHECK(registry.get<Physics::Velocity>(ship) == Physics::Velocity{});
}


TEST_CASE("Cruise: on a profile with a share, a ship thrusts out, coasts, and thrusts in to rest")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	const Space::CruiseDrive drive{.profile = kCoasting};
	registry.replace<Space::CruiseDrive>(ship, drive);
	fixture.Run(5);
	fixture.Events<Space::CruiseStarted>();
	const std::uint64_t departure = fixture.world.Clock().Step();
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	const std::vector<Space::CruiseStarted> started = fixture.Events<Space::CruiseStarted>();
	REQUIRE(started.size() == 1);
	const Space::Cruising cruising = registry.get<Space::Cruising>(ship);
	CHECK(cruising.profile == kCoasting);
	const double least = Space::CruiseSeconds(drive, started[0].metres);
	CHECK(started[0].seconds >= least);
	CHECK(started[0].seconds < least + (4.0 * kStep));
	// Slower than thrusting throughout.
	CHECK(least > Space::CruiseSeconds({.profile = kHard}, started[0].metres) * 1.4);

	const auto relative = [&]
	{
		return registry.get<Sim::Transform>(ship).position -
		       Fixture::Where("Ostra", fixture.world.Clock().Now());
	};
	std::vector<glm::dvec3> places{cruising.course.front(), relative()};
	double hardest = 0.0;
	double coasting = std::numeric_limits<double>::infinity();
	double midSpeed = 0.0;
	const std::uint64_t midway = departure + ((started[0].arrivalStep - departure) / 2);
	while (fixture.world.Clock().Step() + 1 < started[0].arrivalStep)
	{
		fixture.Run(1);
		places.push_back(relative());
		const std::size_t last = places.size() - 1;
		const double acceleration =
		    glm::length(places[last] - (2.0 * places[last - 1]) + places[last - 2]) / (kStep * kStep);
		hardest = std::max(hardest, acceleration);
		if (fixture.world.Clock().Step() == midway)
		{
			coasting = acceleration;
			midSpeed = glm::length(places[last] - places[last - 1]) / kStep;
		}
	}
	const Space::CruiseThrustSeconds thrust = Space::CruiseThrust(kCoasting, started[0].metres);
	const double top = (double{kCoasting.boostAcceleration} * thrust.boost) +
	                   (double{kCoasting.sustainedAcceleration} * thrust.sustained);
	MESSAGE(
	    "Highport to Pell, thrusting a quarter of the time: "
	    << started[0].metres / 1000.0 << " km in " << started[0].seconds << " s, hardest " << hardest
	    << " m/s², coasting at " << midSpeed << " m/s with " << coasting << " m/s²");
	CHECK(hardest <= double{kCoasting.boostAcceleration} * 1.001);
	CHECK(coasting < 1.0);
	// The speed it reached, slowed to the whole steps it was given.
	CHECK(midSpeed == doctest::Approx(top * least / started[0].seconds).epsilon(1.0e-4));
	CHECK(glm::distance(places.back(), cruising.course.back()) < 10.0);

	fixture.Run(1);
	CHECK(registry.get<Physics::LocalFrame>(ship).body == "Pell");
	CHECK(registry.get<Physics::Velocity>(ship) == Physics::Velocity{});
}


TEST_CASE("Cruise: a world saved mid-cruise arrives as the one saved does")
{
	Fixture fixture;
	const entt::entity ship = fixture.Ship("Ostra Highport", {0.0, 0.0, 3000.0});
	SUBCASE("on a profile")
	{
		fixture.world.Registry().replace<Space::CruiseDrive>(
		    ship, Space::CruiseDrive{.profile = kHard});
	}
	SUBCASE("coasting")
	{
		fixture.world.Registry().replace<Space::CruiseDrive>(
		    ship, Space::CruiseDrive{.profile = kCoasting});
	}
	fixture.Run(10);
	fixture.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	fixture.Run(1);
	const std::uint64_t arrival = fixture.world.Registry().get<Space::Cruising>(ship).arrivalStep;
	// Half-way, where a coasting profile is coasting.
	fixture.Run((arrival - fixture.world.Clock().Step()) / 2);
	const std::vector<std::byte> midway = fixture.world.Save();
	fixture.Run(arrival - fixture.world.Clock().Step() + 30);
	const std::uint64_t arrived = fixture.world.StateHash();

	Fixture loaded;
	loaded.world.Load(midway);
	REQUIRE(loaded.world.Registry().all_of<Space::Cruising>(ship));
	loaded.Run(arrival - loaded.world.Clock().Step() + 30);
	CHECK(loaded.world.Registry().get<Physics::LocalFrame>(ship).body == "Pell");
	CHECK(loaded.world.StateHash() == arrived);
}


TEST_CASE(
    "Cruise: a cruise fast-forwarded arrives as the one stepped does, past a ship parked on rails")
{
	Fixture forwarded;
	Fixture stepped;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		fixture->Ship("Ostra Highport", {0.0, 0.0, 3000.0});
		// Kinematic on Highport's rails, at an offset no coordinate's spacing divides.
		entt::registry &registry = fixture->world.Registry();
		const entt::entity parked = registry.create();
		const Physics::OnRails rails{.body = "Ostra Highport", .offset = {40.3, 0.7, -12.1}};
		registry.emplace<Sim::Transform>(
		    parked, fixture->physics.RailsPlace(kHalcyon, rails, fixture->world.Clock().Now()));
		registry.emplace<Sim::InFrame>(parked, kHalcyon);
		registry.emplace<Physics::LocalFrame>(parked, "Ostra Highport");
		registry.emplace<Physics::OnRails>(parked, rails);
		registry.emplace<Physics::Body>(
		    parked, Physics::Body{.kind = Physics::BodyKind::Kinematic, .shape = "hull"});
		fixture->Run(10);
	}
	const entt::entity ship = forwarded.world.Registry().view<Space::CruiseDrive>().front();
	forwarded.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});
	stepped.world.Commands().Push(Space::Cruise{{}, ship, "Pell"});

	// To the step that places the arrival, then on with the ship in physics again; each stop's state
	// as stepping leaves it.
	std::uint64_t end = std::numeric_limits<std::uint64_t>::max();
	int calls = 0;
	while (forwarded.world.Clock().Step() < end)
	{
		forwarded.world.FastForward(end);
		++calls;
		if (const auto *cruising = forwarded.world.Registry().try_get<Space::Cruising>(ship);
		    cruising && end == std::numeric_limits<std::uint64_t>::max())
			end = cruising->arrivalStep + 120;
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}
	CHECK(forwarded.world.Registry().get<Physics::LocalFrame>(ship).body == "Pell");
	// The cruise's 3,000-odd steps in a handful of calls, and each of the 120 after it run.
	MESSAGE("Highport to Pell and two seconds on in " << calls << " calls");
	CHECK(calls < 140);
}
