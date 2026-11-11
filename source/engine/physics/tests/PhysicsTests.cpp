#include <jettison/physics/PhysicsWorld.h>

#include <jettison/core/Assert.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unistd.h>

using namespace Jettison;
using Physics::Body;
using Physics::BodyKind;
using Physics::ShapeDefinition;
using Physics::ShapeKind;
using Physics::Velocity;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
const std::uint64_t kVesper = Sim::SystemId("Vesper");

ShapeDefinition Box(glm::vec3 halfExtents)
{
	ShapeDefinition shape;
	shape.kind = ShapeKind::Box;
	shape.halfExtents = halfExtents;
	return shape;
}

ShapeDefinition Sphere(float radius)
{
	ShapeDefinition shape;
	shape.kind = ShapeKind::Sphere;
	shape.radius = radius;
	return shape;
}

// A square wall across x = 10, 40 m on a side, facing back along -x, where the bodies come from.
ShapeDefinition Wall()
{
	ShapeDefinition shape;
	shape.kind = ShapeKind::Mesh;
	shape.vertices = {
	    {10.0f, -20.0f, -20.0f}, {10.0f, 20.0f, -20.0f}, {10.0f, 20.0f, 20.0f}, {10.0f, -20.0f, 20.0f}};
	shape.indices = {0, 2, 1, 0, 3, 2};
	return shape;
}

Body Dynamic(std::string shape, float mass = 1000.0f)
{
	Body body;
	body.kind = BodyKind::Dynamic;
	body.shape = std::move(shape);
	body.mass = mass;
	return body;
}

// A simulation world with physics, as a game's start-up makes one. Optionally with a system of the
// game's that runs before physics does, as an orbit would.
struct Fixture
{
	explicit Fixture(std::function<void(Sim::SimWorld &)> before = {})
	    : physics{Setup(world, std::move(before))}
	{
	}

	static Physics::PhysicsWorld &Setup(
	    Sim::SimWorld &world, std::function<void(Sim::SimWorld &)> before)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InSystem>();
		if (before)
			before(world);
		Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
		physics.DefineShape("ball", Sphere(1.0f));
		physics.DefineShape("crate", Box({1.0f, 1.0f, 1.0f}));
		physics.DefineShape("hull", Box({10.0f, 10.0f, 10.0f}));
		physics.DefineShape("wall", Wall());
		return physics;
	}

	entt::entity Add(
	    const Body &body, const glm::dvec3 &position, const glm::vec3 &velocity = glm::vec3{0.0f},
	    std::uint64_t system = kHalcyon)
	{
		entt::registry &registry = world.Registry();
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(
		    entity, Sim::Transform{position, glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InSystem>(entity, system);
		registry.emplace<Body>(entity, body);
		if (velocity != glm::vec3{0.0f})
			registry.emplace<Velocity>(entity, velocity, glm::vec3{0.0f});
		return entity;
	}

	void Run(double seconds)
	{
		for (long step = 0; step < std::lround(seconds / kStep); ++step)
			world.Step();
	}

	const glm::dvec3 &Position(entt::entity entity) const
	{
		return world.Registry().get<Sim::Transform>(entity).position;
	}
	const glm::vec3 &Linear(entt::entity entity) const
	{
		return world.Registry().get<Velocity>(entity).linear;
	}

	Sim::SimWorld world{kStep, 1};
	Physics::PhysicsWorld &physics;
};
} // namespace

TEST_CASE("Physics: every kind of shape is built, and one that describes none is refused by name")
{
	Fixture fixture;
	Physics::PhysicsWorld &physics = fixture.physics;

	ShapeDefinition capsule;
	capsule.kind = ShapeKind::Capsule;
	capsule.radius = 0.5f;
	capsule.halfHeight = 1.5f;
	ShapeDefinition cylinder;
	cylinder.kind = ShapeKind::Cylinder;
	cylinder.radius = 2.0f;
	// Thinner than Jolt's rounded edge: the edge is made to fit.
	cylinder.halfHeight = 0.02f;
	ShapeDefinition hull;
	hull.kind = ShapeKind::ConvexHull;
	hull.points = {{0.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f}, {0.0f, 2.0f, 0.0f}, {0.0f, 0.0f, 2.0f}};
	physics.DefineShape("capsule", capsule);
	physics.DefineShape("disc", cylinder);
	physics.DefineShape("tetrahedron", hull);
	physics.DefineShape("speck", Box({0.01f, 0.01f, 0.01f}));
	for (const char *name : {"ball", "crate", "wall", "capsule", "disc", "tetrahedron", "speck"})
		CHECK(physics.HasShape(name));
	CHECK_FALSE(physics.HasShape("nothing"));

	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("ball", Sphere(2.0f)), doctest::Contains("'ball' is already defined"),
	    std::invalid_argument);
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("flat", Box({1.0f, 0.0f, 1.0f})),
	    doctest::Contains("'flat' has a half extent of 0"), std::invalid_argument);
	ShapeDefinition triangle = hull;
	triangle.points.pop_back();
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("triangle", triangle), doctest::Contains("a hull of 3 points"),
	    std::invalid_argument);
	ShapeDefinition point = hull;
	point.points.assign(4, glm::vec3{1.0f, 2.0f, 3.0f});
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("point", point), doctest::Contains("'point' is refused by Jolt"),
	    std::invalid_argument);
	ShapeDefinition torn = Wall();
	torn.indices.pop_back();
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("torn", torn), doctest::Contains("5 indices, which is not whole triangles"),
	    std::invalid_argument);
	torn = Wall();
	torn.indices[4] = 4;
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("torn", torn), doctest::Contains("an index past its 4 vertices"),
	    std::invalid_argument);
	CHECK_FALSE(physics.HasShape("torn"));
}

TEST_CASE("Physics: shapes are definitions a mod can override")
{
	const std::filesystem::path directory =
	    std::filesystem::temp_directory_path() / ("jettison-physics-tests-" + std::to_string(getpid()));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory / "base/shapes/ships");
	std::filesystem::create_directories(directory / "mod/shapes/ships");
	std::ofstream{directory / "base/shapes/ships/courier.yaml"}
	    << "version: 1\nkind: Capsule\nradius: 2\nhalfHeight: 6\n";
	std::ofstream{directory / "base/shapes/crate.yaml"}
	    << "version: 1\nkind: Box\nhalfExtents: [1, 1, 1]\n";
	std::ofstream{directory / "mod/shapes/ships/courier.yaml"}
	    << "version: 1\nkind: Sphere\nradius: 3\n";

	Vfs::FileSystem fileSystem;
	fileSystem.AddMount({"base", directory / "base", false});
	fileSystem.AddMount({"mod", directory / "mod", false});
	Physics::RegisterTypes();
	Sim::DefinitionRegistry definitions{fileSystem};
	definitions.Load<ShapeDefinition>("shapes", Serialise::FormatVersion{1});
	REQUIRE(definitions.Find<ShapeDefinition>("ships/courier"));
	CHECK(definitions.Find<ShapeDefinition>("ships/courier")->kind == ShapeKind::Sphere);

	Sim::SimWorld world{kStep, 1};
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
	physics.DefineShapes(definitions);
	CHECK(physics.HasShape("ships/courier"));
	CHECK(physics.HasShape("crate"));
	std::filesystem::remove_all(directory);
}

TEST_CASE("Physics: two bodies meet, and part as their restitution has them")
{
	Fixture fixture;
	Body ball = Dynamic("ball");
	ball.restitution = 1.0f;
	const entt::entity left = fixture.Add(ball, {-5.0, 0.0, 0.0}, {10.0f, 0.0f, 0.0f});
	const entt::entity right = fixture.Add(ball, {5.0, 0.0, 0.0}, {-10.0f, 0.0f, 0.0f});
	CHECK(fixture.physics.BodyCount() == 0);

	fixture.world.Step();
	CHECK(fixture.physics.BodyCount() == 2);
	CHECK(fixture.physics.Systems() == std::vector{kHalcyon});
	CHECK(fixture.Position(left).x == doctest::Approx(-5.0 + 10.0 * kStep));

	// They meet 0.4 s in, and an elastic collision of equal masses swaps their velocities.
	fixture.Run(1.0);
	CHECK(fixture.Linear(left).x == doctest::Approx(-10.0).epsilon(0.02));
	CHECK(fixture.Linear(right).x == doctest::Approx(10.0).epsilon(0.02));
	CHECK(fixture.Position(right).x - fixture.Position(left).x > 2.0);
	CHECK(fixture.Position(left).y == 0.0);

	// Inelastic, a crate stops at a static wall, and does not go through it.
	Body crate = Dynamic("crate");
	Body wall;
	wall.kind = BodyKind::Static;
	wall.shape = "wall";
	fixture.Add(wall, {0.0, 100.0, 0.0});
	const entt::entity thrown = fixture.Add(crate, {0.0, 100.0, 0.0}, {20.0f, 0.0f, 0.0f});
	fixture.Run(1.5);
	CHECK(std::abs(fixture.Linear(thrown).x) < 0.5f);
	CHECK(fixture.Position(thrown).x < 9.0);
	CHECK(fixture.Position(thrown).x > 8.0);
}

TEST_CASE("Physics: a kinematic station on its orbit stops a ship that flies into it")
{
	// The station moves at 7.7 km/s along x, as Halcyon's station does about its planet, its
	// Transform written by an orbit system before physics runs.
	constexpr double kOrbitalSpeed = 7700.0;
	Fixture fixture{[](Sim::SimWorld &world)
	                {
		                world.RegisterSystem(
		                    {"orbit",
		                     Ecs::TickPhase::Simulation,
		                     Ecs::Access::Of<>().Writing<Sim::Transform>(),
		                     {},
		                     [](entt::registry &registry, double step)
		                     {
			                     for (auto [entity, body, transform] :
			                          registry.view<const Body, Sim::Transform>().each())
			                     {
				                     if (body.kind == BodyKind::Kinematic)
					                     transform.position.x += kOrbitalSpeed * step;
			                     }
		                     }});
	                }};
	Body station;
	station.kind = BodyKind::Kinematic;
	station.shape = "hull";
	const glm::dvec3 origin{1.496e11, 0.0, 4.2e7};
	const entt::entity hub = fixture.Add(station, origin);
	// The ship keeps pace with the station, closing on it at 5 m/s from 19 m off its face.
	const entt::entity ship =
	    fixture.Add(Dynamic("crate"), origin - glm::dvec3{30.0, 0.0, 0.0}, {7705.0f, 0.0f, 0.0f});

	fixture.Run(6.0);
	const double gap = fixture.Position(hub).x - fixture.Position(ship).x;
	CHECK(fixture.Position(hub).x == doctest::Approx(origin.x + kOrbitalSpeed * 6.0));
	// Against the station's face, at the station's speed: Jolt's speed limit is the settings',
	// not its own 500 m/s.
	CHECK(gap == doctest::Approx(11.0).epsilon(0.01));
	CHECK(fixture.Linear(ship).x == doctest::Approx(kOrbitalSpeed).epsilon(1e-4));
}

TEST_CASE("Physics: what the game writes between steps is what the body takes")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Add(Dynamic("crate"), {0.0, 0.0, 0.0}, {1.0f, 0.0f, 0.0f});
	fixture.world.Step();
	CHECK(fixture.Position(ship).x == doctest::Approx(kStep));

	// Placed elsewhere, it moves on from there.
	registry.get<Sim::Transform>(ship).position = {500.0, 0.0, 0.0};
	fixture.world.Step();
	CHECK(fixture.Position(ship).x == doctest::Approx(500.0 + kStep));

	// Given a velocity, it takes it, and spinning, it turns.
	registry.get<Velocity>(ship) = {{0.0f, 3.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
	fixture.Run(1.0);
	CHECK(fixture.Position(ship).y == doctest::Approx(3.0).epsilon(0.01));
	const glm::dquat turned = registry.get<Sim::Transform>(ship).orientation;
	CHECK(glm::angle(turned) == doctest::Approx(1.0).epsilon(0.01));

	// A changed Body is made again where the entity is, keeping its velocity.
	registry.get<Body>(ship).mass = 50.0f;
	const glm::dvec3 before = fixture.Position(ship);
	fixture.world.Step();
	CHECK(fixture.physics.BodyCount() == 1);
	CHECK(fixture.Linear(ship).y == doctest::Approx(3.0f));
	CHECK(fixture.Position(ship).y == doctest::Approx(before.y + 3.0 * kStep));

	// A static body moved is moved.
	Body wall;
	wall.kind = BodyKind::Static;
	wall.shape = "wall";
	const entt::entity barrier = fixture.Add(wall, {0.0, 0.0, 0.0});
	fixture.world.Step();
	registry.get<Sim::Transform>(barrier).position.x = fixture.Position(ship).x - 5.0;
	registry.get<Velocity>(ship) = {{30.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
	fixture.Run(1.0);
	CHECK(fixture.Position(ship).x < registry.get<Sim::Transform>(barrier).position.x + 10.0);
}

TEST_CASE("Physics: a body is in its system's world, and leaves it with its entity or its system")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	// Two systems' balls at the same coordinates are a system apart; two in one system overlap and
	// are pushed out of each other.
	const entt::entity here = fixture.Add(Dynamic("ball"), {0.0, 0.0, 0.0});
	const entt::entity there = fixture.Add(Dynamic("ball"), {0.5, 0.0, 0.0}, glm::vec3{0.0f}, kVesper);
	fixture.Run(0.5);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 1);
	CHECK(fixture.physics.BodyCount(kVesper) == 1);
	CHECK(fixture.Position(here) == glm::dvec3{0.0});
	CHECK(fixture.Position(there) == glm::dvec3{0.5, 0.0, 0.0});

	registry.get<Sim::InSystem>(there).system = kHalcyon;
	fixture.Run(0.5);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 2);
	CHECK(fixture.physics.BodyCount(kVesper) == 0);
	CHECK(fixture.physics.Systems() == std::vector{kHalcyon});
	CHECK(glm::distance(fixture.Position(here), fixture.Position(there)) > 1.9);

	// On a lane it is in no world; arrived, it is in one again.
	registry.get<Sim::InSystem>(there).system = Sim::kNoSystem;
	fixture.world.Step();
	CHECK(fixture.physics.BodyCount() == 1);
	registry.get<Sim::InSystem>(there).system = kVesper;
	fixture.world.Step();
	CHECK(fixture.physics.BodyCount(kVesper) == 1);

	registry.remove<Body>(here);
	registry.destroy(there);
	fixture.world.Step();
	CHECK(fixture.physics.BodyCount() == 0);
	CHECK(fixture.physics.Systems().empty());
}

TEST_CASE("Physics: a body that cannot be made is refused before the step changes anything")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity ship = fixture.Add(Dynamic("crate"), {0.0, 0.0, 0.0}, {1.0f, 0.0f, 0.0f});
	fixture.world.Step();
	const glm::dvec3 before = fixture.Position(ship);

	const entt::entity ghost = fixture.Add(Dynamic("phantom"), {50.0, 0.0, 0.0});
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("names shape 'phantom', which is not defined"),
	    std::invalid_argument);
	CHECK(fixture.Position(ship) == before);
	CHECK(fixture.physics.BodyCount() == 1);

	registry.get<Body>(ghost).shape = "wall";
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("its shape 'wall' is a mesh"), std::invalid_argument);
	registry.get<Body>(ghost) = Dynamic("crate", 0.0f);
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("is dynamic with a mass of 0 kg"),
	    std::invalid_argument);
	Body slick = Dynamic("crate");
	slick.friction = std::nanf("");
	registry.get<Body>(ghost) = slick;
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("friction, restitution or damping of nan"),
	    std::invalid_argument);

	CHECK_THROWS_AS(
	    fixture.physics.CastRay(kHalcyon, {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 10.0),
	    Core::NotImplementedError);
	Sim::SimWorld world{kStep, 1};
	CHECK_THROWS_AS(
	    Physics::PhysicsWorld::Attach(world, Physics::PhysicsSettings{.maxBodies = 0}),
	    std::invalid_argument);
	CHECK_THROWS_AS(Physics::PhysicsWorld::Attach(fixture.world), std::logic_error);
}

TEST_CASE("Physics: a system full of bodies refuses the next")
{
	Sim::SimWorld world{kStep, 1};
	Sim::RegisterTypes();
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Sim::InSystem>();
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world, {.maxBodies = 2});
	physics.DefineShape("ball", Sphere(1.0f));
	entt::registry &registry = world.Registry();
	const auto add = [&](double x)
	{
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(entity, Sim::Transform{{x, 0.0, 0.0}});
		registry.emplace<Sim::InSystem>(entity, kHalcyon);
		registry.emplace<Body>(entity, Dynamic("ball"));
		return entity;
	};
	add(0.0);
	const entt::entity second = add(10.0);
	world.Step();
	add(20.0);
	CHECK_THROWS_WITH_AS(world.Step(), doctest::Contains("has its 2 already"), std::invalid_argument);
	// One leaving makes room in the same step.
	registry.destroy(second);
	world.Step();
	CHECK(physics.BodyCount() == 2);
}
