#include "Systems.h"

#include <jettison/ai/Lanes.h>
#include <jettison/ai/Steering.h>
#include <jettison/math/Random.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/SystemIndices.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace Jettison;
using Ai::Agent;
using Ai::Obstacle;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
constexpr std::string_view kStation = "Ostra Highport";

// A ship of the default thrusters: 150 m/s, braking at half its weakest thruster's 15 m/s².
Agent Ship(const glm::dvec3 &position, const glm::dvec3 &velocity = glm::dvec3{0.0})
{
	return {position, velocity, 150.0, 7.5, 7.0};
}

// A step of a point mass whose velocity goes towards the one demanded at up to 15 m/s², as flight
// assist's does: what the behaviours are judged by without the flight model's turning.
void Fly(Agent &agent, const glm::dvec3 &demanded)
{
	const glm::dvec3 change = demanded - agent.velocity;
	const double most = 15.0 * kStep;
	const double length = glm::length(change);
	agent.velocity += length > most ? change * (most / length) : change;
	agent.position += agent.velocity * kStep;
}

double Gap(const Agent &agent, const Obstacle &obstacle)
{
	return glm::distance(agent.position, obstacle.position) - agent.radius - obstacle.radius;
}
} // namespace

TEST_CASE("Steering: seek goes at full speed, and arrive slows to stop at the target")
{
	const Agent agent = Ship({0.0, 0.0, 0.0});
	CHECK(Ai::Seek(agent, {300.0, 400.0, 0.0}) == glm::dvec3{90.0, 120.0, 0.0});
	CHECK(Ai::Seek(agent, {0.0, 0.0, 0.0}) == glm::dvec3{0.0});

	// Far off, full speed; within braking range √(2ad); in the last metres the distance a second.
	CHECK(glm::length(Ai::Arrive(agent, {0.0, 0.0, -5000.0})) == doctest::Approx(150.0));
	CHECK(glm::length(Ai::Arrive(agent, {0.0, 0.0, -100.0})) == doctest::Approx(std::sqrt(1500.0)));
	CHECK(Ai::Arrive(agent, {0.0, 3.0, 0.0}) == glm::dvec3{0.0, 3.0, 0.0});
	// A target that moves is arrived at relative to it.
	CHECK(Ai::Arrive(agent, {0.0, 0.0, 0.0}, {1.0, 2.0, 3.0}) == glm::dvec3{1.0, 2.0, 3.0});

	// Flown from 2 km off, it stops at the target without passing it by more than a centimetre.
	Agent flown = Ship({0.0, 0.0, 2000.0});
	double nearest = std::numeric_limits<double>::infinity();
	for (int step = 0; step < 60 * 60; ++step)
	{
		Fly(flown, Ai::Arrive(flown, {0.0, 0.0, 0.0}));
		nearest = std::min(nearest, flown.position.z);
	}
	CHECK(nearest > -0.01);
	CHECK(glm::length(flown.position) < 0.01);
	CHECK(glm::length(flown.velocity) < 0.01);
}

TEST_CASE("Steering: avoidance turns aside from what is ahead and leaves the rest alone")
{
	const Agent agent = Ship({0.0, 0.0, 0.0});
	const glm::dvec3 ahead{0.0, 0.0, -60.0};
	const std::vector<Obstacle> station{{{0.0, 10.0, -400.0}, {0.0, 0.0, 0.0}, 80.0}};

	// Passing 10 m from the centre of a station 400 m ahead, inside its 107 m: turned down, away
	// from its centre, at the shortfall over the time to it, and no faster towards it.
	const glm::dvec3 turned = Ai::Avoid(agent, ahead, station, 8.0, 20.0);
	CHECK(turned.y < -10.0);
	CHECK(turned.x == 0.0);
	CHECK(turned.z == doctest::Approx(-60.0));
	// Further than the lookahead, moving away, or passing clear: as desired.
	CHECK(Ai::Avoid(agent, ahead, station, 4.0, 20.0) == ahead);
	CHECK(Ai::Avoid(agent, -ahead, station, 8.0, 20.0) == -ahead);
	CHECK(Ai::Avoid(agent, {60.0, 0.0, 0.0}, station, 8.0, 20.0) == glm::dvec3{60.0, 0.0, 0.0});
	CHECK(Ai::Avoid(agent, ahead, {}, 8.0, 20.0) == ahead);

	// Two ships meeting head on each turn their own way.
	const Agent east = Ship({-200.0, 0.0, 0.0});
	const Agent west = Ship({200.0, 0.0, 0.0});
	const glm::dvec3 eastward{50.0, 0.0, 0.0};
	const std::vector<Obstacle> westShip{{west.position, -eastward, west.radius}};
	const std::vector<Obstacle> eastShip{{east.position, eastward, east.radius}};
	const glm::dvec3 eastTurn = Ai::Avoid(east, eastward, westShip, 5.0, 20.0) - eastward;
	const glm::dvec3 westTurn = Ai::Avoid(west, -eastward, eastShip, 5.0, 20.0) + eastward;
	CHECK(glm::length(eastTurn) > 1.0);
	CHECK(glm::dot(eastTurn, westTurn) == doctest::Approx(-glm::dot(eastTurn, eastTurn)));

	// Nothing closes on an obstacle faster than it can stop before the clearance, and nothing
	// at all once within it.
	const Obstacle wall{{0.0, 0.0, -100.0}, {0.0, 0.0, 0.0}, 50.0};
	const glm::dvec3 slowed = Ai::Avoid(agent, ahead, std::span{&wall, 1}, 0.1, 20.0);
	CHECK(-slowed.z == doctest::Approx(std::min(std::sqrt(2.0 * 7.5 * 23.0), 23.0)));
	const Agent within = Ship({0.0, 0.0, -30.0});
	CHECK(Ai::Avoid(within, ahead, std::span{&wall, 1}, 0.1, 20.0).z > 0.0);
}

TEST_CASE("Steering: a ship flown at a station's centre goes round it and on to its target")
{
	Agent agent = Ship({0.0, 0.0, 1500.0});
	const std::vector<Obstacle> station{{{0.0, 0.0, 0.0}, {0.0, 0.0, 0.0}, 80.0}};
	double closest = std::numeric_limits<double>::infinity();
	for (int step = 0; step < 60 * 90; ++step)
	{
		const glm::dvec3 desired = Ai::Arrive(agent, {0.0, 0.0, -1500.0});
		Fly(agent, Ai::Avoid(agent, desired, station, 5.0, 20.0));
		closest = std::min(closest, Gap(agent, station[0]));
	}
	CHECK(closest > 10.0);
	CHECK(glm::distance(agent.position, glm::dvec3{0.0, 0.0, -1500.0}) < 0.1);
}

TEST_CASE("Steering: separation pushes apart only what is nearer than it")
{
	const Agent agent = Ship({0.0, 0.0, 0.0});
	const std::vector<Obstacle> near{{{20.0, 0.0, 0.0}, {}, 7.0}, {{0.0, -30.0, 0.0}, {}, 7.0}};
	// 34 m short of the 54 m along x, and 24 m along y.
	CHECK(Ai::Separate(agent, near, 40.0) == glm::dvec3{-34.0, 24.0, 0.0});
	CHECK(Ai::Separate(agent, near, 7.0) == glm::dvec3{-1.0, 0.0, 0.0});
	CHECK(Ai::Separate(agent, {}, 40.0) == glm::dvec3{0.0});
	const Obstacle same{agent.position, {}, 7.0};
	CHECK(Ai::Separate(agent, std::span{&same, 1}, 40.0) == glm::dvec3{0.0});
}

namespace
{
// A world as a game's start-up makes one: physics, the spatial index and steering, with Ostra
// Highport a kinematic body on its rails in its own frame, and ships of a shuttle's hull.
struct Fixture
{
	Sim::SimWorld world{kStep, 5};
	Physics::PhysicsWorld &physics{Setup(world)};
	Spatial::SystemIndices &indices{Spatial::SystemIndices::Attach(world)};
	Ai::Steering &steering{Ai::Steering::Attach(world, physics, indices)};
	entt::entity station{entt::null};
	// Every contact begun between two bodies, neither a sensor.
	std::vector<Physics::ContactEvent> collisions;

	Fixture()
	{
		Physics::ShapeDefinition shape;
		shape.halfExtents = {40.0f, 40.0f, 60.0f};
		physics.DefineShape("station", shape);
		shape.halfExtents = {2.5f, 1.5f, 6.0f};
		physics.DefineShape("shuttle", shape);
		physics.DefineStarSystem(AiTests::Halcyon());

		entt::registry &registry = world.Registry();
		station = registry.create();
		registry.emplace<Sim::Transform>(station, Sim::Transform{StationAt(0.0)});
		registry.emplace<Sim::InSystem>(station, kHalcyon);
		registry.emplace<Physics::OnRails>(station, std::string{kStation});
		registry.emplace<Physics::LocalFrame>(station, std::string{kStation});
		registry.emplace<Physics::Body>(
		    station, Physics::Body{Physics::BodyKind::Kinematic, "station"});
	}

	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		return Physics::PhysicsWorld::Attach(world);
	}

	static glm::dvec3 StationAt(double time)
	{
		const Celestial::StarSystem halcyon = AiTests::Halcyon();
		return Celestial::EvaluateBody(halcyon, static_cast<std::size_t>(halcyon.Find(kStation)), time)
		    .metres;
	}

	// A steered ship at rest that far from the station, in its frame, nose along -Z.
	entt::entity Ship(const glm::dvec3 &offset, const Ai::Destination &destination)
	{
		entt::registry &registry = world.Registry();
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(ship, Sim::Transform{StationAt(world.Clock().Now()) + offset});
		registry.emplace<Sim::InSystem>(ship, kHalcyon);
		registry.emplace<Physics::LocalFrame>(ship, std::string{kStation});
		Physics::Body body{Physics::BodyKind::Dynamic, "shuttle", 20000.0f};
		body.continuous = true;
		registry.emplace<Physics::Body>(ship, body);
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::Thrusters>(ship);
		registry.emplace<Physics::FlightControls>(ship);
		registry.emplace<Ai::Steered>(ship);
		registry.emplace<Ai::Destination>(ship, destination);
		return ship;
	}

	void Step()
	{
		world.Step();
		for (const Physics::ContactEvent &contact : physics.Contacts())
			if (contact.phase == Physics::ContactPhase::Began && !contact.sensor)
				collisions.push_back(contact);
	}

	void Run(double seconds)
	{
		for (long step = 0; step < std::lround(seconds / kStep); ++step)
			Step();
	}

	// Steps until every ship has arrived, or the time is up; the seconds it took, or none.
	std::optional<double> RunUntilArrived(std::span<const entt::entity> ships, double seconds)
	{
		for (long step = 0; step < std::lround(seconds / kStep); ++step)
		{
			Step();
			const bool all = std::ranges::all_of(
			    ships,
			    [&](entt::entity ship) { return world.Registry().get<Ai::Destination>(ship).arrived; });
			if (all)
				return static_cast<double>(step + 1) * kStep;
		}
		return std::nullopt;
	}

	// Where an entity is relative to the station, as the last step left both.
	glm::dvec3 FromStation(entt::entity entity) const
	{
		const entt::registry &registry = world.Registry();
		return registry.get<Sim::Transform>(entity).position -
		       registry.get<Sim::Transform>(station).position;
	}

	template <typename T> std::vector<T> Events()
	{
		std::vector<T> found;
		for (const Sim::SimEvent &event : world.Events().Take())
			if (const T *value = event.event.try_cast<T>())
				found.push_back(*value);
		return found;
	}
};

Ai::Destination Point(const glm::dvec3 &offset, float speed = 0.0f)
{
	Ai::Destination destination;
	destination.offset = offset;
	destination.speed = speed;
	return destination;
}
} // namespace

TEST_CASE("Steering: steering runs after the spatial index and before the flight model")
{
	Fixture fixture;
	fixture.world.RegisterSystem(
	    {"game", Ecs::TickPhase::Simulation, {}, {}, [](entt::registry &, double) {}});
	const std::vector<std::string> order = fixture.world.Scheduler().Order(Ecs::TickPhase::Simulation);
	const auto at = [&](std::string_view name)
	{ return std::ranges::find(order, name) - order.begin(); };
	CHECK(at("spatial") < at("steering"));
	CHECK(at("steering") < at("flight"));
	CHECK(at("flight") < at("physics"));
}

TEST_CASE("Steering: a ship flies to a point by the station, arrives at rest, and faces its way")
{
	Fixture fixture;
	const glm::dvec3 from{300.0, 0.0, 0.0};
	const glm::dvec3 to{300.0, 200.0, -1200.0};
	const entt::entity ship = fixture.Ship(from, Point(to));
	fixture.Step();
	const std::optional<double> took = fixture.RunUntilArrived(std::span{&ship, 1}, 120.0);
	REQUIRE(took);
	// 1.2 km at up to 150 m/s, accelerating and braking: about 20 s.
	CHECK(*took < 30.0);
	const std::vector<Ai::Arrived> arrived = fixture.Events<Ai::Arrived>();
	REQUIRE(arrived.size() == 1);
	CHECK(arrived[0].ship == ship);
	CHECK(arrived[0].entity == entt::null);

	// Held there, in the station's frame as it orbits.
	fixture.Run(10.0);
	CHECK(glm::distance(fixture.FromStation(ship), to) < 2.0);
	CHECK(glm::length(fixture.world.Registry().get<Physics::Velocity>(ship).linear) < 0.5f);
	const glm::dvec3 nose =
	    fixture.world.Registry().get<Sim::Transform>(ship).orientation * glm::dvec3{0.0, 0.0, -1.0};
	CHECK(glm::dot(nose, glm::normalize(to - from)) > 0.98);
	CHECK(fixture.collisions.empty());
	CHECK(fixture.Events<Ai::Arrived>().empty());
}

TEST_CASE("Steering: a ship whose way is through the station flies round it without touching")
{
	Fixture fixture;
	const entt::entity ship = fixture.Ship({0.0, 0.0, 1500.0}, Point({0.0, 0.0, -1500.0}, 80.0f));
	double nearest = std::numeric_limits<double>::infinity();
	for (int step = 0; step < 60 * 120 && !fixture.world.Registry().get<Ai::Destination>(ship).arrived;
	     ++step)
	{
		fixture.Step();
		nearest = std::min(nearest, glm::length(fixture.FromStation(ship)));
	}
	CHECK(fixture.world.Registry().get<Ai::Destination>(ship).arrived);
	CHECK(fixture.collisions.empty());
	// The station's 82 m sphere, the ship's 6.7 m and the 20 m clearance.
	CHECK(nearest > 100.0);
}

TEST_CASE("Steering: a course's waypoints are flown through in order")
{
	Fixture fixture;
	Ai::Destination destination = Point({0.0, 0.0, -600.0});
	destination.waypoints = {{400.0, 0.0, 0.0}, {400.0, 0.0, -600.0}};
	const entt::entity ship = fixture.Ship({0.0, 0.0, 300.0}, destination);
	std::vector<std::uint32_t> passed;
	for (int step = 0; step < 60 * 120 && !fixture.world.Registry().get<Ai::Destination>(ship).arrived;
	     ++step)
	{
		fixture.Step();
		const std::uint32_t next = fixture.world.Registry().get<Ai::Destination>(ship).next;
		if (passed.empty() || passed.back() != next)
		{
			passed.push_back(next);
			if (next > 0)
				CHECK(
				    glm::distance(fixture.FromStation(ship), destination.waypoints[next - 1]) <= 45.0);
		}
	}
	CHECK(passed == std::vector<std::uint32_t>{0, 1, 2});
	CHECK(fixture.world.Registry().get<Ai::Destination>(ship).arrived);
	CHECK(fixture.collisions.empty());
}

TEST_CASE("Steering: a destination by an entity is its offset in the entity's axes")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	// The station turned a quarter about Y: its +X is the system's -Z.
	registry.get<Physics::OnRails>(fixture.station).turn =
	    glm::angleAxis(glm::radians(90.0), glm::dvec3{0.0, 1.0, 0.0});
	Ai::Destination destination;
	destination.entity = fixture.station;
	destination.offset = {300.0, 0.0, 0.0};
	const entt::entity ship = fixture.Ship({0.0, 0.0, 500.0}, destination);
	fixture.RunUntilArrived(std::span{&ship, 1}, 120.0);
	CHECK(registry.get<Ai::Destination>(ship).arrived);
	CHECK(glm::distance(fixture.FromStation(ship), glm::dvec3{0.0, 0.0, -300.0}) < 5.0);
	CHECK(fixture.Events<Ai::Arrived>().at(0).entity == fixture.station);

	// An entity that goes takes the destination with it, and the ship holds still.
	registry.destroy(fixture.station);
	fixture.Step();
	const std::vector<Ai::DestinationLost> lost = fixture.Events<Ai::DestinationLost>();
	REQUIRE(lost.size() == 1);
	CHECK(lost[0].ship == ship);
	CHECK(lost[0].reason == "its destination has gone");
	CHECK_FALSE(registry.all_of<Ai::Destination>(ship));
	CHECK(registry.get<Physics::FlightControls>(ship) == Physics::FlightControls{});
}

TEST_CASE("Steering: the commands set a destination from its start and take it away")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	Ai::Destination far = Point({0.0, 0.0, -2000.0});
	far.next = 3;
	far.arrived = true;
	const entt::entity ship = fixture.Ship({0.0, 0.0, 500.0}, Point({0.0, 0.0, 500.0}));
	fixture.world.Commands().Push(Ai::SetDestination{ship, far});
	fixture.Step();
	CHECK(registry.get<Ai::Destination>(ship).next == 0);
	CHECK_FALSE(registry.get<Ai::Destination>(ship).arrived);
	fixture.Run(2.0);
	CHECK(registry.get<Physics::FlightControls>(ship).linear.z < -0.5f);

	fixture.world.Commands().Push(Ai::ClearDestination{ship});
	fixture.world.Commands().Push(Ai::ClearDestination{entt::entity{12345}});
	fixture.Step();
	CHECK_FALSE(registry.all_of<Ai::Destination>(ship));
	CHECK(registry.get<Physics::FlightControls>(ship) == Physics::FlightControls{});
}

namespace
{
// The exit criterion: twelve ships from all round the station, 1 to 2 km off, each flying to its
// own place on a ring 250 m about it, their ways crossing at the station.
std::vector<entt::entity> Gathering(Fixture &fixture)
{
	Math::Random random{2026};
	std::vector<entt::entity> ships;
	constexpr int kShips = 12;
	for (int index = 0; index < kShips; ++index)
	{
		glm::dvec3 direction{0.0};
		while (glm::length(direction) < 0.1 || glm::length(direction) > 1.0)
			direction = {
			    random.NextDouble() * 2.0 - 1.0, random.NextDouble() * 2.0 - 1.0,
			    random.NextDouble() * 2.0 - 1.0};
		const double distance = 1000.0 + random.NextDouble() * 1000.0;
		// Its place is across the ring from where it starts, more or less, so the ways cross.
		const double angle = glm::two_pi<double>() * (static_cast<double>(index) / kShips + 0.5);
		const glm::dvec3 place{250.0 * std::cos(angle), 0.0, 250.0 * std::sin(angle)};
		const glm::dvec3 start = glm::normalize(direction) * distance;
		ships.push_back(fixture.Ship(start, Point(place, 80.0f)));
	}
	return ships;
}

// The nearest any two ships' hulls came, and the nearest any ship's came to the station's.
struct Nearest
{
	double ships{std::numeric_limits<double>::infinity()};
	double station{std::numeric_limits<double>::infinity()};
};

void Measure(const Fixture &fixture, std::span<const entt::entity> ships, Nearest &nearest)
{
	constexpr double kShip = 6.68;
	for (std::size_t first = 0; first < ships.size(); ++first)
	{
		const glm::dvec3 at = fixture.FromStation(ships[first]);
		nearest.station = std::min(nearest.station, glm::length(at) - kShip - 82.46);
		for (std::size_t second = first + 1; second < ships.size(); ++second)
			nearest.ships = std::min(
			    nearest.ships, glm::distance(at, fixture.FromStation(ships[second])) - 2.0 * kShip);
	}
}
} // namespace

TEST_CASE("Steering: a group of ships reaches a station without colliding")
{
	Fixture fixture;
	const std::vector<entt::entity> ships = Gathering(fixture);
	Nearest nearest;
	std::optional<double> took;
	for (int step = 0; step < 60 * 150 && !took; ++step)
	{
		fixture.Step();
		Measure(fixture, ships, nearest);
		if (std::ranges::all_of(
		        ships, [&](entt::entity ship)
		        { return fixture.world.Registry().get<Ai::Destination>(ship).arrived; }))
			took = (step + 1) * kStep;
	}
	REQUIRE(took);
	MESSAGE(
	    "Twelve ships arrived in ", *took, " s; nearest between hulls ", nearest.ships,
	    " m, to the station's ", nearest.station, " m");
	CHECK(fixture.collisions.empty());
	CHECK(nearest.ships > 5.0);
	CHECK(nearest.station > 5.0);
	CHECK(fixture.Events<Ai::Arrived>().size() == ships.size());
}

TEST_CASE("Steering: the same start gives the same flight, and a save part-way carries on as it")
{
	const auto run = [](std::optional<std::vector<std::byte>> &saved, int saveAt)
	{
		Fixture fixture;
		const std::vector<entt::entity> ships = Gathering(fixture);
		for (int step = 0; step < 60 * 40; ++step)
		{
			if (step == saveAt)
				saved = fixture.world.Save();
			fixture.Step();
		}
		return fixture.world.StateHash();
	};
	std::optional<std::vector<std::byte>> save;
	std::optional<std::vector<std::byte>> unused;
	const std::uint64_t first = run(save, 60 * 15);
	CHECK(run(unused, -1) == first);
	// Recorded from a Debug build and checked by the Release suite and under the sanitisers too: a
	// change here is a change to how steering flies, or to what a save holds, and either is a
	// decision, not drift. 9c's destination saves whether it is kept clear by its hull and which
	// way it faces; without those fields saved the flight still hashes to 9b's 0x7b531286df2bea60.
	CHECK(first == 0x348528439a61a5e0ull);

	Fixture loaded;
	(void)Gathering(loaded);
	loaded.world.Load(*save);
	for (int step = 60 * 15; step < 60 * 40; ++step)
		loaded.Step();
	CHECK(loaded.world.StateHash() == first);
}
