#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>

#include <jettison/celestial/StarSystem.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

using namespace Jettison;
using Physics::Body;
using Physics::BodyKind;
using Physics::FlightControls;
using Physics::LocalFrame;
using Physics::OnRails;
using Physics::SetFlightControls;
using Physics::ShapeDefinition;
using Physics::ShapeKind;
using Physics::Thrusters;
using Physics::Velocity;

namespace
{
constexpr double kStep = 1.0 / 60.0;
constexpr std::uint64_t kSeed = 0x5ee1;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
const std::uint64_t kEmpty = Sim::SystemId("Empty");
constexpr std::string_view kStation = "Ostra Highport";

// Halcyon as its system file has it (assets/systems/halcyon.yaml): a Sun-like star, an Earth-like
// planet a hundred and fifty million kilometres out, and a station in low orbit over it, so the
// station moves at 7.7 km/s about the planet and 30 km/s about the star.
Celestial::StarSystem Halcyon()
{
	Celestial::StarSystem system;
	system.name = "Halcyon";
	Celestial::Body star;
	star.name = "Halcyon";
	star.kind = Celestial::BodyKind::Star;
	star.radius = 6.957e8;
	star.gravitationalParameter = 1.32712440018e20;
	Celestial::Body planet;
	planet.name = "Ostra";
	planet.parent = "Halcyon";
	planet.radius = 6.371e6;
	planet.gravitationalParameter = 3.986004418e14;
	planet.orbit = {1.496e11, 0.0167, 0.0, 0.0, 1.7967, 6.24, 0.0};
	Celestial::Body station;
	station.name = std::string{kStation};
	station.kind = Celestial::BodyKind::Station;
	station.parent = "Ostra";
	station.radius = 60.0;
	station.orbit = {6.791e6, 0.0005, 0.9006, 1.2, 0.4, 3.1416, 0.0};
	system.bodies = {star, planet, station};
	Celestial::Resolve(system);
	return system;
}

// A closed box of twelve triangles facing out, a station's hull as a mesh: what a kinematic body
// on rails is made of.
ShapeDefinition MeshBox(const glm::vec3 &half)
{
	ShapeDefinition shape;
	shape.kind = ShapeKind::Mesh;
	for (const float x : {-half.x, half.x})
		for (const float y : {-half.y, half.y})
			for (const float z : {-half.z, half.z})
				shape.vertices.emplace_back(x, y, z);
	// Vertex index bits: 4 is x, 2 is y, 1 is z. Counter-clockwise seen from outside.
	shape.indices = {0, 3, 2, 0, 1, 3, 4, 7, 5, 4, 6, 7, 0, 5, 1, 0, 4, 5,
	                 2, 7, 6, 2, 3, 7, 0, 6, 4, 0, 2, 6, 1, 7, 3, 1, 5, 7};
	return shape;
}

// The game's start-up, the same every run: the station's hull, a panel, a ship's hull and a ball.
Physics::PhysicsWorld &Populate(Sim::SimWorld &world)
{
	Physics::RegisterTypes();
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Sim::InSystem>();
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
	physics.DefineStarSystem(Halcyon());
	physics.DefineShape("hub", MeshBox({6.0f, 6.0f, 30.0f}));
	physics.DefineShape("panel", MeshBox({12.0f, 7.0f, 0.075f}));
	ShapeDefinition shape;
	shape.kind = ShapeKind::Box;
	shape.halfExtents = {1.5f, 0.8f, 4.0f};
	physics.DefineShape("hull", shape);
	shape.kind = ShapeKind::Sphere;
	shape.radius = 0.25f;
	physics.DefineShape("ball", shape);
	return physics;
}

// The station, kinematic and on rails, in its own frame unless it is to be seen in the system's.
entt::entity Station(Sim::SimWorld &world, std::string_view shape, bool framed = true)
{
	entt::registry &registry = world.Registry();
	const entt::entity station = registry.create();
	registry.emplace<Sim::Transform>(station);
	registry.emplace<Sim::InSystem>(station, kHalcyon);
	registry.emplace<OnRails>(station, std::string{kStation});
	if (framed)
		registry.emplace<LocalFrame>(station, std::string{kStation});
	registry.emplace<Body>(station, Body{BodyKind::Kinematic, std::string{shape}});
	return station;
}

// Where the station is at a time, and how fast it is moving there as the step from it moves it.
Sim::Transform StationAt(double simSeconds)
{
	const Celestial::StarSystem system = Halcyon();
	return {Celestial::EvaluateBody(system, 2, simSeconds).metres, glm::dquat{1.0, 0.0, 0.0, 0.0}};
}
glm::dvec3 StationVelocity(double simSeconds)
{
	return (StationAt(simSeconds + kStep).position - StationAt(simSeconds).position) / kStep;
}

// A dynamic body beside the station, in its frame and still in it: a ship that has been holding
// station.
entt::entity Beside(
    Sim::SimWorld &world, std::string_view shape, float mass, const glm::dvec3 &offset,
    const glm::dquat &orientation, bool continuous)
{
	entt::registry &registry = world.Registry();
	const double now = world.Clock().Now();
	const entt::entity ship = registry.create();
	registry.emplace<Sim::Transform>(ship, StationAt(now).position + offset, orientation);
	registry.emplace<Sim::InSystem>(ship, kHalcyon);
	Body body{BodyKind::Dynamic, std::string{shape}, mass};
	body.restitution = 0.3f;
	body.continuous = continuous;
	registry.emplace<Body>(ship, body);
	registry.emplace<LocalFrame>(ship, std::string{kStation});
	registry.emplace<Velocity>(ship);
	return ship;
}

// A ship alone in a system with no gravity, at rest: for the flight model on its own.
entt::entity Ship(Sim::SimWorld &world, const glm::dquat &orientation = {1.0, 0.0, 0.0, 0.0})
{
	entt::registry &registry = world.Registry();
	const entt::entity ship = registry.create();
	registry.emplace<Sim::Transform>(ship, glm::dvec3{0.0}, orientation);
	registry.emplace<Sim::InSystem>(ship, kEmpty);
	registry.emplace<Body>(ship, Body{BodyKind::Dynamic, "hull", 20000.0f});
	registry.emplace<Velocity>(ship);
	registry.emplace<Thrusters>(ship);
	return ship;
}

void Steps(Sim::SimWorld &world, int count)
{
	for (int step = 0; step < count; ++step)
		world.Step();
}

void Fly(Sim::SimWorld &world, entt::entity ship, const FlightControls &controls)
{
	world.Commands().Push(SetFlightControls{ship, controls});
}

const Velocity &VelocityOf(const Sim::SimWorld &world, entt::entity entity)
{
	return world.Registry().get<Velocity>(entity);
}

// Where the ship is in the station's frame, which is the system's turned by nothing: the station on
// rails does not spin.
glm::dvec3 Relative(const Sim::SimWorld &world, entt::entity ship, entt::entity station)
{
	const entt::registry &registry = world.Registry();
	return registry.get<Sim::Transform>(ship).position - registry.get<Sim::Transform>(station).position;
}
glm::dvec3 RelativeVelocity(const Sim::SimWorld &world, entt::entity ship, entt::entity station)
{
	return glm::dvec3{VelocityOf(world, ship).linear} - glm::dvec3{VelocityOf(world, station).linear};
}
} // namespace

TEST_CASE("Flight: with assist, a demand is a speed the thrusters reach and hold")
{
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	const entt::entity ship = Ship(world);
	world.Step();

	// Full ahead: 30 m/s^2 along -Z, for two seconds, then held at the 150 m/s it reaches.
	Fly(world, ship, {{0.0f, 0.0f, -1.0f}});
	Steps(world, 120);
	CHECK(VelocityOf(world, ship).linear.z == doctest::Approx(-60.0).epsilon(1e-4));
	CHECK(VelocityOf(world, ship).linear.x == 0.0f);
	Steps(world, 300);
	CHECK(VelocityOf(world, ship).linear.z == doctest::Approx(-150.0).epsilon(1e-5));

	// Let go, it stops, braking astern at 15 m/s^2, and stays stopped.
	Fly(world, ship, {});
	Steps(world, 60);
	CHECK(VelocityOf(world, ship).linear.z == doctest::Approx(-135.0).epsilon(1e-4));
	Steps(world, 600);
	CHECK(VelocityOf(world, ship).linear.z == 0.0f);

	// Half to starboard and a quarter up: shares of the 150 m/s, reached together.
	Fly(world, ship, {{0.5f, 0.25f, 0.0f}});
	Steps(world, 600);
	CHECK(VelocityOf(world, ship).linear.x == doctest::Approx(75.0).epsilon(1e-5));
	CHECK(VelocityOf(world, ship).linear.y == doctest::Approx(37.5).epsilon(1e-5));

	// A demand past full is full, and one that is not a number is none.
	Fly(world, ship, {{-4.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f}});
	Steps(world, 1200);
	CHECK(VelocityOf(world, ship).linear.x == doctest::Approx(-150.0).epsilon(1e-5));
	CHECK(VelocityOf(world, ship).linear.y == 0.0f);
}

TEST_CASE("Flight: without assist, a demand is an acceleration, and a ship let go coasts")
{
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	const entt::entity ship = Ship(world);
	world.Step();

	Fly(world, ship, {{0.0f, 0.0f, -0.5f}, {}, false});
	Steps(world, 60);
	CHECK(VelocityOf(world, ship).linear.z == doctest::Approx(-15.0).epsilon(1e-4));
	Fly(world, ship, {{}, {}, false});
	Steps(world, 600);
	CHECK(VelocityOf(world, ship).linear.z == doctest::Approx(-15.0).epsilon(1e-4));

	// A turn begun spins on.
	Fly(world, ship, {{}, {0.0f, 1.0f, 0.0f}, false});
	Steps(world, 30);
	Fly(world, ship, {{}, {}, false});
	Steps(world, 120);
	CHECK(VelocityOf(world, ship).angular.y == doctest::Approx(1.0).epsilon(1e-3));
}

TEST_CASE("Flight: turns are held at their rates with assist, and stop when let go")
{
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	const entt::entity ship = Ship(world);
	world.Step();

	// Yaw to port, about +Y: 2 rad/s^2 to 0.9 rad/s, in under half a second.
	Fly(world, ship, {{}, {0.0f, 1.0f, 0.0f}});
	Steps(world, 60);
	CHECK(VelocityOf(world, ship).angular.y == doctest::Approx(0.9).epsilon(1e-4));
	CHECK(std::abs(VelocityOf(world, ship).angular.x) < 1e-4f);
	Fly(world, ship, {});
	Steps(world, 60);
	CHECK(VelocityOf(world, ship).angular.y == 0.0f);

	// Turned to port, full ahead goes the way the ship points.
	const glm::dquat orientation = world.Registry().get<Sim::Transform>(ship).orientation;
	const glm::dvec3 forward = orientation * glm::dvec3{0.0, 0.0, -1.0};
	Fly(world, ship, {{0.0f, 0.0f, -1.0f}});
	Steps(world, 60);
	const glm::dvec3 velocity{VelocityOf(world, ship).linear};
	CHECK(glm::length(velocity) == doctest::Approx(30.0).epsilon(1e-3));
	CHECK(glm::dot(glm::normalize(velocity), forward) == doctest::Approx(1.0).epsilon(1e-6));
}

TEST_CASE("Flight: the thrusters' accelerations do not depend on the ship's mass")
{
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	const entt::entity light = Ship(world);
	const entt::entity heavy = Ship(world);
	world.Registry().get<Sim::Transform>(heavy).position.x = 100.0;
	world.Registry().get<Body>(heavy).mass = 900000.0f;
	world.Step();
	for (const entt::entity ship : {light, heavy})
		Fly(world, ship, {{0.0f, 0.3f, -1.0f}, {0.2f, 0.0f, 0.5f}});
	Steps(world, 45);
	CHECK(VelocityOf(world, light) == VelocityOf(world, heavy));
}

TEST_CASE("Flight: assist holds a ship to its reference, not to the system")
{
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	// A hulk coasting at 7 km/s, and a ship beside it at its speed.
	const entt::entity hulk = Ship(world);
	world.Registry().remove<Thrusters>(hulk);
	world.Registry().get<Sim::Transform>(hulk).position.x = -500.0;
	world.Registry().get<Velocity>(hulk).linear = {7000.0f, 0.0f, 0.0f};
	const entt::entity ship = Ship(world);
	world.Registry().get<Velocity>(ship).linear = {7000.0f, 0.0f, 0.0f};
	world.Step();

	Fly(world, ship, {{}, {}, true, hulk});
	Steps(world, 120);
	CHECK(VelocityOf(world, ship).linear.x == 7000.0f);
	Fly(world, ship, {{1.0f, 0.0f, 0.0f}, {}, true, hulk});
	Steps(world, 600);
	CHECK(VelocityOf(world, ship).linear.x == doctest::Approx(7150.0).epsilon(1e-6));

	// The reference gone, the ship is held to the system frame, and brakes towards rest in it.
	world.Registry().destroy(hulk);
	Fly(world, ship, {{}, {}, true, hulk});
	Steps(world, 60);
	CHECK(VelocityOf(world, ship).linear.x == doctest::Approx(7150.0 - 15.0).epsilon(1e-6));

	// The command for a ship that is gone does nothing.
	world.Registry().destroy(ship);
	Fly(world, ship, {{1.0f, 0.0f, 0.0f}});
	CHECK_NOTHROW(world.Step());
}

TEST_CASE("Flight: an entity on rails is where its body is as each step ends")
{
	Sim::SimWorld world{kStep, kSeed};
	Physics::PhysicsWorld &physics = Populate(world);
	CHECK(physics.HasStarSystem(kHalcyon));
	CHECK_THROWS_AS(physics.DefineStarSystem(Halcyon()), std::invalid_argument);
	Celestial::StarSystem unresolved = Halcyon();
	unresolved.name = "Unresolved";
	unresolved.bodies[2].parentIndex = -1;
	CHECK_THROWS_WITH_AS(
	    physics.DefineStarSystem(unresolved), doctest::Contains("is not resolved"),
	    std::invalid_argument);

	// In the system's frame, and in its own, which it is still in.
	const entt::entity station = Station(world, "hub", false);
	const entt::entity framed = Station(world, "hub");
	Steps(world, 2);
	const double before = world.Clock().Now();
	world.Step();
	const Sim::Transform &placed = world.Registry().get<Sim::Transform>(station);
	CHECK(placed.position == StationAt(world.Clock().Now()).position);
	CHECK(world.Registry().get<Sim::Transform>(framed).position == placed.position);
	CHECK(physics.FrameCount(kHalcyon) == 2);
	// Moved with its orbit's velocity from the first step: the 7.7 km/s about the planet and the
	// 30 km/s about the star, to within what a float holds.
	const glm::dvec3 expected = StationVelocity(before);
	CHECK(glm::length(expected) > 2.0e4);
	CHECK(glm::length(glm::dvec3{VelocityOf(world, station).linear} - expected) < 0.05);
	CHECK(glm::length(VelocityOf(world, framed).linear) < 0.01f);
	world.Registry().destroy(framed);

	// One on no system stays where it is.
	world.Registry().get<Sim::InSystem>(station).system = Sim::kNoSystem;
	const glm::dvec3 left = placed.position;
	world.Step();
	CHECK(world.Registry().get<Sim::Transform>(station).position == left);
}

TEST_CASE("Flight: rails that lead nowhere are refused before the step changes anything")
{
	Sim::SimWorld world{kStep, kSeed};
	Physics::PhysicsWorld &physics = Populate(world);
	const entt::entity station = Station(world, "hub");
	world.Registry().get<OnRails>(station).body = "Ostra Lowport";
	const std::uint64_t before = world.StateHash();
	CHECK_THROWS_WITH_AS(
	    world.Step(), doctest::Contains("has no body 'Ostra Lowport'"), std::invalid_argument);
	CHECK(physics.BodyCount() == 0);

	world.Registry().get<OnRails>(station).body = std::string{kStation};
	world.Registry().get<Sim::InSystem>(station).system = kEmpty;
	CHECK_THROWS_WITH_AS(world.Step(), doctest::Contains("is not defined"), std::invalid_argument);
	CHECK(physics.BodyCount() == 0);
	world.Registry().get<Sim::InSystem>(station).system = kHalcyon;
	world.Registry().get<OnRails>(station).body = "Ostra Lowport";
	CHECK(world.StateHash() == before);
}

TEST_CASE("Flight: gravity is the system's bodies pulling, and none outside a defined system")
{
	Sim::SimWorld world{kStep, kSeed};
	const Physics::PhysicsWorld &physics = Populate(world);
	const Celestial::StarSystem system = Halcyon();
	const glm::dvec3 planet = Celestial::EvaluateBody(system, 1, 0.0).metres;
	const glm::dvec3 star{0.0};
	const glm::dvec3 up = glm::normalize(planet - star);
	const auto starPull = [&](const glm::dvec3 &at)
	{ return glm::normalize(star - at) * (1.32712440018e20 / glm::dot(at - star, at - star)); };

	// At the station's height, 8.6 m/s^2 towards the planet, and the star's 6 mm/s^2 besides.
	const glm::dvec3 high = planet + up * 6.791e6;
	const glm::dvec3 pull = physics.Gravity(kHalcyon, high, 0.0) - starPull(high);
	CHECK(glm::length(pull) == doctest::Approx(3.986004418e14 / (6.791e6 * 6.791e6)).epsilon(1e-9));
	CHECK(glm::dot(glm::normalize(pull), -up) == doctest::Approx(1.0).epsilon(1e-12));
	// Halfway to the centre, half the surface's.
	const glm::dvec3 deep = planet + up * 3.1855e6;
	CHECK(
	    glm::length(physics.Gravity(kHalcyon, deep, 0.0) - starPull(deep)) ==
	    doctest::Approx(0.5 * 3.986004418e14 / (6.371e6 * 6.371e6)).epsilon(1e-9));
	CHECK(physics.Gravity(kEmpty, high, 0.0) == glm::dvec3{0.0});
}

TEST_CASE("Flight: a ship let drift beside a station on rails falls round the planet with it")
{
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	const entt::entity station = Station(world, "hub");
	const entt::entity ship =
	    Beside(world, "hull", 20000.0f, {0.0, 0.0, -200.0}, {1.0, 0.0, 0.0, 0.0}, true);
	world.Step();
	const glm::dvec3 start = Relative(world, ship, station);

	// A minute with nothing flying it. Without gravity it would be 15 km off, the planet's 8.6 m/s^2
	// pulling the station round and not the ship; with it, what is left is the tide, the planet
	// pulling harder on whichever is nearer it, and the Moon, which pulls the ship and not the rails.
	// Measured at 5 mm on 2026-11-11; in the system's frame, where a velocity is a float at 30 km/s,
	// it was 1.8 m.
	Steps(world, 3600);
	const double drift = glm::length(Relative(world, ship, station) - start);
	MESSAGE("drift over a minute: " << drift << " m");
	CHECK(drift < 0.05);
}

TEST_CASE("Flight: a fast body swept continuously meets a thin panel that a placed one passes")
{
	// A ball fired at a panel 15 cm thick at 240 m/s relative, 4 m a step, both moving at the
	// station's 30 km/s about the star: Jolt sweeps a continuous body against the panel by their
	// relative motion, so the 500 m it moves each step in the system frame does not matter.
	for (const bool continuous : {false, true})
	{
		CAPTURE(continuous);
		Sim::SimWorld world{kStep, kSeed};
		Populate(world);
		const entt::entity panel = Station(world, "panel");
		const entt::entity ball =
		    Beside(world, "ball", 10.0f, {0.0, 0.0, -21.0}, {1.0, 0.0, 0.0, 0.0}, continuous);
		world.Registry().get<Velocity>(ball).linear += glm::vec3{0.0f, 0.0f, 240.0f};
		Steps(world, 30);
		const double z = Relative(world, ball, panel).z;
		if (continuous)
			CHECK(z < 0.0);
		else
			CHECK(z > 0.0);
	}
}

namespace
{
// The exit's run: a ship holding station 150 m off Highport's hub, nose towards it, flown in at
// 60 m/s, rolling, which meets the hub's end and is knocked askew, slides across it pushing ahead,
// is let go, and is brought to rest by its assist.
constexpr std::uint64_t kApproachSteps = 480;

struct Approach
{
	entt::entity station;
	entt::entity ship;
};

Approach Begin(Sim::SimWorld &world)
{
	const entt::entity station = Station(world, "hub");
	// A half turn about Y, so its -Z, ahead, points along the system's +Z, at the hub's end.
	const entt::entity ship = Beside(
	    world, "hull", 20000.0f, {0.0, 1.5, -190.0},
	    glm::angleAxis(glm::pi<double>(), glm::dvec3{0, 1, 0}), true);
	world.Registry().emplace<Thrusters>(ship);
	return {station, ship};
}

void Feed(Sim::SimWorld &world, const Approach &approach)
{
	const FlightControls holding{{}, {}, true, approach.station};
	switch (world.Clock().Step())
	{
		case 0: Fly(world, approach.ship, holding); break;
		case 10: Fly(world, approach.ship, {{0.0f, 0.0f, -0.4f}, {}, true, approach.station}); break;
		// A roll on the way in, which turns the hull about the line it flies along.
		case 60:
			Fly(world, approach.ship,
			    {{0.0f, 0.0f, -0.4f}, {0.0f, 0.0f, 0.1f}, true, approach.station});
			break;
		case 300: Fly(world, approach.ship, holding); break;
		default: break;
	}
}

void Run(Sim::SimWorld &world, const Approach &approach, std::uint64_t until, double *closest = nullptr)
{
	while (world.Clock().Step() < until)
	{
		Feed(world, approach);
		world.Step();
		if (closest)
			*closest = std::min(*closest, -Relative(world, approach.ship, approach.station).z);
	}
}
} // namespace

TEST_CASE("Flight replay: a ship flown into a station on its orbit meets it, the same in every build")
{
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	world.SetRecording(true);
	const Approach approach = Begin(world);
	double closest = std::numeric_limits<double>::max();
	Run(world, approach, 270, &closest);

	// It hit the hub's end, 30 m from the station's centre: the ship's centre came no nearer than its
	// own half-width, in any attitude, from it, and the approach at 60 m/s is over.
	CHECK(closest < 36.0);
	CHECK(closest > 30.0 + 0.8 - 0.05);
	CHECK(RelativeVelocity(world, approach.ship, approach.station).z < 5.0);

	// Let go, the assist brings it to rest relative to the station, which is moving at 30 km/s.
	Run(world, approach, kApproachSteps, &closest);
	CHECK(glm::length(RelativeVelocity(world, approach.ship, approach.station)) < 0.05);
	CHECK(-Relative(world, approach.ship, approach.station).z < 40.0);
	CHECK(closest > 30.0 + 0.8 - 0.05);

	// Recorded from a Debug build and checked by the Release suite and under the sanitisers.
	CHECK(world.StateHash() == 0xce15aba2a75c5a79ull);

	// The recorded commands replay it from the start.
	REQUIRE(world.Recorded().size() == 4);
	Sim::SimWorld replay{kStep, kSeed};
	Populate(replay);
	Begin(replay);
	std::size_t next = 0;
	while (replay.Clock().Step() < kApproachSteps)
	{
		for (; next < world.Recorded().size() && world.Recorded()[next].step == replay.Clock().Step();
		     ++next)
			replay.Commands().Push(world.Recorded()[next].command);
		replay.Step();
	}
	CHECK(replay.StateHash() == world.StateHash());
}

TEST_CASE("Flight replay: saved in contact with the station and loaded, it carries on the same")
{
	Sim::SimWorld original{kStep, kSeed};
	Populate(original);
	const Approach approach = Begin(original);
	Run(original, approach, 250);
	const std::vector<std::byte> save = original.Save();

	Sim::SimWorld loaded{kStep, 1};
	Populate(loaded);
	loaded.Load(save);
	CHECK(loaded.Save() == save);
	Run(original, approach, kApproachSteps);
	Run(loaded, approach, kApproachSteps);
	CHECK(loaded.Save() == original.Save());
}
