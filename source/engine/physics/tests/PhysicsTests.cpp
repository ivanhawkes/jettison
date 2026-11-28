#include <jettison/physics/PhysicsWorld.h>

#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
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
const std::uint64_t kHalcyon = Sim::FrameId("Halcyon");
const std::uint64_t kVesper = Sim::FrameId("Vesper");


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
		world.RegisterComponent<Sim::InFrame>();
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
		registry.emplace<Sim::InFrame>(entity, system);
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
	    physics.DefineShape("triangle", triangle), doctest::Contains("a convex hull of 3 points"),
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


TEST_CASE("Physics: a compound shape and its parts' offsets and turns are read from a shape file")
{
	const std::filesystem::path directory = std::filesystem::temp_directory_path() /
	                                        ("jettison-physics-compound-" + std::to_string(getpid()));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory / "shapes");
	std::ofstream{directory / "shapes/dumbbell.yaml"} << "version: 1\n"
	                                                     "kind: Compound\n"
	                                                     "offset: [0, 1, 0]\n"
	                                                     "parts:\n"
	                                                     "  - kind: Sphere\n"
	                                                     "    radius: 1\n"
	                                                     "    offset: [-3, 0, 0]\n"
	                                                     "  - kind: Box\n"
	                                                     "    halfExtents: [0.2, 3, 0.2]\n"
	                                                     "    turn: [0, 0, 0.70710678, 0.70710678]\n";

	Vfs::FileSystem fileSystem;
	fileSystem.AddMount({"base", directory, false});
	Physics::RegisterTypes();
	Sim::DefinitionRegistry definitions{fileSystem};
	definitions.Load<ShapeDefinition>("shapes", Serialise::FormatVersion{1});
	const ShapeDefinition *dumbbell = definitions.Find<ShapeDefinition>("dumbbell");
	REQUIRE(dumbbell);
	CHECK(dumbbell->kind == ShapeKind::Compound);
	CHECK(dumbbell->offset == glm::vec3{0.0f, 1.0f, 0.0f});
	REQUIRE(dumbbell->parts.size() == 2);
	CHECK(dumbbell->parts[0].offset == glm::vec3{-3.0f, 0.0f, 0.0f});
	CHECK(dumbbell->parts[1].turn.z == doctest::Approx(0.70710678f));
	CHECK(dumbbell->parts[1].turn.w == doctest::Approx(0.70710678f));

	// Written as read, and read back the same.
	const std::string written = Serialise::YamlArchive::Write(*dumbbell, Serialise::FormatVersion{1});
	const auto reread =
	    Serialise::YamlArchive::Read<ShapeDefinition>(written, Serialise::FormatVersion{1});
	CHECK(Serialise::YamlArchive::Write(reread, Serialise::FormatVersion{1}) == written);

	// The bar, turned to lie along x below the body's origin moved up 1 m, is hit from above.
	Sim::SimWorld world{kStep, 1};
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Sim::InFrame>();
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
	physics.DefineShapes(definitions);
	entt::registry &registry = world.Registry();
	const entt::entity entity = registry.create();
	registry.emplace<Sim::Transform>(entity, Sim::Transform{{0.0, 0.0, 0.0}});
	registry.emplace<Sim::InFrame>(entity, kHalcyon);
	registry.emplace<Body>(entity, Body{BodyKind::Static, "dumbbell"});
	world.Step();
	const std::optional<Physics::CastHit> hit =
	    physics.CastRay(kHalcyon, {2.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(hit);
	CHECK(hit->distance == doctest::Approx(8.8).epsilon(1e-4));
	CHECK(hit->part == 1);
	std::filesystem::remove_all(directory);
}


TEST_CASE("Physics: a shape sits in its body at its offset and turn")
{
	Fixture fixture;
	Physics::PhysicsWorld &physics = fixture.physics;

	// A bar 4 m long along x, stood upright and raised 3 m: from y = 1 to y = 5 over the body's origin.
	ShapeDefinition lever = Box({2.0f, 0.5f, 0.5f});
	lever.offset = {0.0f, 3.0f, 0.0f};
	lever.turn = glm::angleAxis(glm::radians(90.0f), glm::vec3{0.0f, 0.0f, 1.0f});
	physics.DefineShape("lever", lever);
	CHECK(
	    physics.ShapeRadius("lever") ==
	    doctest::Approx(glm::length(glm::vec3{2.0f, 0.5f, 0.5f}) + 3.0));

	Body post;
	post.kind = BodyKind::Static;
	post.shape = "lever";
	const entt::entity body = fixture.Add(post, {0.0, 0.0, 0.0});
	fixture.world.Step();

	const std::optional<Physics::CastHit> top =
	    physics.CastRay(kHalcyon, {0.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(top);
	CHECK(top->entity == body);
	CHECK(top->distance == doctest::Approx(5.0).epsilon(1e-4));
	CHECK(top->part == 0);
	const std::optional<Physics::CastHit> side =
	    physics.CastRay(kHalcyon, {10.0, 3.0, 0.0}, {-1.0, 0.0, 0.0}, 20.0);
	REQUIRE(side);
	CHECK(side->distance == doctest::Approx(9.5).epsilon(1e-4));
	CHECK_FALSE(physics.CastRay(kHalcyon, {10.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, 20.0));

	ShapeDefinition unturned = Sphere(1.0f);
	unturned.turn = glm::quat{0.0f, 0.0f, 0.0f, 0.0f};
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("unturned", unturned),
	    doctest::Contains("'unturned' has a turn that is not a rotation"), std::invalid_argument);
	ShapeDefinition nowhere = Sphere(1.0f);
	nowhere.offset.y = std::nanf("");
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("nowhere", nowhere),
	    doctest::Contains("'nowhere' has a point that is not finite"), std::invalid_argument);
}


TEST_CASE("Physics: a compound body is its parts, each placed, and a hit names the part")
{
	Fixture fixture;
	Physics::PhysicsWorld &physics = fixture.physics;

	// Two balls 6 m apart on a thin bar.
	ShapeDefinition dumbbell;
	dumbbell.kind = ShapeKind::Compound;
	dumbbell.parts = {Sphere(1.0f), Sphere(1.0f), Box({3.0f, 0.2f, 0.2f})};
	dumbbell.parts[0].offset = {-3.0f, 0.0f, 0.0f};
	dumbbell.parts[1].offset = {3.0f, 0.0f, 0.0f};
	physics.DefineShape("dumbbell", dumbbell);
	CHECK(physics.ShapeRadius("dumbbell") == doctest::Approx(4.0));

	Body still;
	still.kind = BodyKind::Static;
	still.shape = "dumbbell";
	const entt::entity body = fixture.Add(still, {0.0, 0.0, 0.0});
	fixture.world.Step();

	const std::optional<Physics::CastHit> right =
	    physics.CastRay(kHalcyon, {10.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, 20.0);
	REQUIRE(right);
	CHECK(right->entity == body);
	CHECK(right->distance == doctest::Approx(6.0).epsilon(1e-4));
	CHECK(right->part == 1);
	const std::optional<Physics::CastHit> left =
	    physics.CastRay(kHalcyon, {-10.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, 20.0);
	REQUIRE(left);
	CHECK(left->part == 0);
	const std::optional<Physics::CastHit> bar =
	    physics.CastRay(kHalcyon, {0.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(bar);
	CHECK(bar->distance == doctest::Approx(9.8).epsilon(1e-4));
	CHECK(bar->part == 2);

	// Every part along the ray is one body, met once, at its nearest part.
	const std::vector<Physics::CastHit> through =
	    physics.CastRayAll(kHalcyon, {10.0, 0.0, 0.0}, {-1.0, 0.0, 0.0}, 20.0);
	REQUIRE(through.size() == 1);
	CHECK(through.front().part == 1);
	const std::optional<Physics::CastHit> swept =
	    physics.CastShape(kHalcyon, "ball", Sim::Transform{{3.0, 10.0, 0.0}}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(swept);
	CHECK(swept->distance == doctest::Approx(8.0).epsilon(1e-3));
	CHECK(swept->part == 1);
	CHECK(physics.OverlapSphere(kHalcyon, {3.0, 1.5, 0.0}, 1.0) == std::vector{body});
	CHECK(physics.OverlapSphere(kHalcyon, {0.0, 1.5, 0.0}, 1.0).empty());

	// A compound is cast and overlapped as any shape is: here against a crate at its right ball,
	// and short of the wall across x = 10 by the 4 m it reaches.
	Body wall;
	wall.kind = BodyKind::Static;
	wall.shape = "wall";
	const entt::entity across = fixture.Add(wall, {0.0, 100.0, 0.0});
	const entt::entity crate = fixture.Add(Dynamic("crate"), {3.0, 200.0, 0.0});
	fixture.world.Step();
	const std::optional<Physics::CastHit> pushed = physics.CastShape(
	    kHalcyon, "dumbbell", Sim::Transform{{0.0, 100.0, 0.0}}, {1.0, 0.0, 0.0}, 20.0);
	REQUIRE(pushed);
	CHECK(pushed->entity == across);
	CHECK(pushed->distance == doctest::Approx(6.0).epsilon(1e-3));
	CHECK(
	    physics.Overlap(kHalcyon, "dumbbell", Sim::Transform{{0.0, 200.0, 0.0}}) == std::vector{crate});
	CHECK(physics.Overlap(kHalcyon, "dumbbell", Sim::Transform{{0.0, 202.5, 0.0}}).empty());

	// Dynamic, it flies as one body and meets the wall with its leading ball.
	const entt::entity thrown =
	    fixture.Add(Dynamic("dumbbell"), {0.0, 100.0, 0.0}, {10.0f, 0.0f, 0.0f});
	fixture.Run(1.5);
	CHECK(fixture.physics.Touching(thrown) == std::vector{across});
	CHECK(fixture.Position(thrown).x < 6.1);
	CHECK(fixture.Position(thrown).x > 5.0);
}


TEST_CASE("Physics: a compound of nothing is refused, a part by its index, and a mesh in one is a mesh")
{
	Fixture fixture;
	Physics::PhysicsWorld &physics = fixture.physics;
	ShapeDefinition empty;
	empty.kind = ShapeKind::Compound;
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("empty", empty), doctest::Contains("'empty' is a compound of no parts"),
	    std::invalid_argument);
	ShapeDefinition broken = empty;
	broken.parts = {Sphere(1.0f), Sphere(0.0f)};
	CHECK_THROWS_WITH_AS(
	    physics.DefineShape("broken", broken), doctest::Contains("'broken, part 1' has a radius of 0"),
	    std::invalid_argument);
	CHECK_FALSE(physics.HasShape("broken"));

	// One part alone is built as that part, and still a compound to whoever reads its parts.
	ShapeDefinition single = empty;
	single.parts = {Sphere(1.0f)};
	single.parts[0].offset = {0.0f, 2.0f, 0.0f};
	physics.DefineShape("single", single);
	Body still;
	still.kind = BodyKind::Static;
	still.shape = "single";
	fixture.Add(still, {0.0, 0.0, 0.0});
	fixture.world.Step();
	const std::optional<Physics::CastHit> hit =
	    physics.CastRay(kHalcyon, {0.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
	REQUIRE(hit);
	CHECK(hit->distance == doctest::Approx(7.0).epsilon(1e-4));
	CHECK(hit->part == 0);

	ShapeDefinition walled = empty;
	walled.parts = {Sphere(1.0f), Wall()};
	physics.DefineShape("walled", walled);
	CHECK_THROWS_WITH_AS(
	    physics.CastShape(kHalcyon, "walled", Sim::Transform{}, {1.0, 0.0, 0.0}, 1.0),
	    doctest::Contains("'walled' is a mesh"), std::invalid_argument);
	CHECK_THROWS_WITH_AS(
	    physics.Overlap(kHalcyon, "walled", Sim::Transform{}), doctest::Contains("'walled' is a mesh"),
	    std::invalid_argument);
	fixture.Add(Dynamic("walled"), {50.0, 0.0, 0.0});
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("its shape 'walled' is a mesh"), std::invalid_argument);
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
	CHECK(fixture.physics.Frames() == std::vector{kHalcyon});
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

	registry.get<Sim::InFrame>(there).frame = kHalcyon;
	fixture.Run(0.5);
	CHECK(fixture.physics.BodyCount(kHalcyon) == 2);
	CHECK(fixture.physics.BodyCount(kVesper) == 0);
	CHECK(fixture.physics.Frames() == std::vector{kHalcyon});
	CHECK(glm::distance(fixture.Position(here), fixture.Position(there)) > 1.9);

	// On a lane it is in no world; arrived, it is in one again.
	registry.get<Sim::InFrame>(there).frame = Sim::kNoFrame;
	fixture.world.Step();
	CHECK(fixture.physics.BodyCount() == 1);
	registry.get<Sim::InFrame>(there).frame = kVesper;
	fixture.world.Step();
	CHECK(fixture.physics.BodyCount(kVesper) == 1);

	registry.remove<Body>(here);
	registry.destroy(there);
	fixture.world.Step();
	CHECK(fixture.physics.BodyCount() == 0);
	CHECK(fixture.physics.Frames().empty());
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
	world.RegisterComponent<Sim::InFrame>();
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world, {.maxBodies = 2});
	physics.DefineShape("ball", Sphere(1.0f));
	entt::registry &registry = world.Registry();
	const auto add = [&](double x)
	{
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(entity, Sim::Transform{{x, 0.0, 0.0}});
		registry.emplace<Sim::InFrame>(entity, kHalcyon);
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


namespace
{
// Two layers: a ghost meets no layer of the game's, not even its own, and a ship meets ships.
void DefineGhostsAndShips(Physics::PhysicsWorld &physics)
{
	physics.DefineLayers({{"ghost", {}}, {"ship", {{"ship"}}}});
}


Body InLayer(Body body, std::string layer)
{
	body.layer = std::move(layer);
	return body;
}


Body InGroup(Body body, std::uint32_t group)
{
	body.group = group;
	return body;
}


// Whether any contact the step began or ended names the entity.
bool InContacts(const Physics::PhysicsWorld &physics, entt::entity entity)
{
	return std::ranges::any_of(
	    physics.Contacts(), [entity](const Physics::ContactEvent &contact)
	    { return contact.first == entity || contact.second == entity; });
}
} // namespace


TEST_CASE("Physics: a game's layers decide what meets what, and a body in none meets every layer")
{
	Fixture fixture;
	DefineGhostsAndShips(fixture.physics);
	Body ball = Dynamic("ball");
	ball.restitution = 1.0f;
	const entt::entity ghostLeft =
	    fixture.Add(InLayer(ball, "ghost"), {-5.0, 0.0, 0.0}, {10.0f, 0.0f, 0.0f});
	const entt::entity ghostRight =
	    fixture.Add(InLayer(ball, "ghost"), {5.0, 0.0, 0.0}, {-10.0f, 0.0f, 0.0f});
	const entt::entity shipLeft =
	    fixture.Add(InLayer(ball, "ship"), {-5.0, 50.0, 0.0}, {10.0f, 0.0f, 0.0f});
	const entt::entity shipRight =
	    fixture.Add(InLayer(ball, "ship"), {5.0, 50.0, 0.0}, {-10.0f, 0.0f, 0.0f});

	// A ghost meets a static wall in no layer as any body does.
	Body wall;
	wall.kind = BodyKind::Static;
	wall.shape = "wall";
	const entt::entity barrier = fixture.Add(wall, {0.0, 100.0, 0.0});
	const entt::entity thrown =
	    fixture.Add(InLayer(Dynamic("crate"), "ghost"), {0.0, 100.0, 0.0}, {20.0f, 0.0f, 0.0f});

	bool ghostsMet = false;
	for (int step = 0; step < 90; ++step)
	{
		fixture.world.Step();
		ghostsMet = ghostsMet || InContacts(fixture.physics, ghostLeft) ||
		            InContacts(fixture.physics, ghostRight);
	}

	// The ghosts pass through each other untouched; the ships meet and swap their velocities.
	CHECK_FALSE(ghostsMet);
	CHECK(fixture.Linear(ghostLeft).x == doctest::Approx(10.0));
	CHECK(fixture.Position(ghostLeft).x > 5.0);
	CHECK(fixture.Linear(shipLeft).x == doctest::Approx(-10.0).epsilon(0.02));
	CHECK(fixture.Linear(shipRight).x == doctest::Approx(10.0).epsilon(0.02));
	CHECK(std::abs(fixture.Linear(thrown).x) < 0.5f);
	CHECK(fixture.Position(thrown).x < 9.0);
	CHECK(fixture.physics.Touching(thrown) == std::vector{barrier});
}


TEST_CASE("Physics: a cast or an overlap as a layer hits only what a body of that layer would meet")
{
	Fixture fixture;
	DefineGhostsAndShips(fixture.physics);
	Physics::PhysicsWorld &physics = fixture.physics;
	Body wall;
	wall.kind = BodyKind::Static;
	wall.shape = "wall";
	const entt::entity barrier = fixture.Add(wall, {0.0, 0.0, 0.0});
	const entt::entity ghost = fixture.Add(InLayer(Dynamic("ball"), "ghost"), {0.0, 0.0, 0.0});
	const entt::entity ship = fixture.Add(InLayer(Dynamic("ball"), "ship"), {4.0, 0.0, 0.0});
	fixture.world.Step();

	const glm::dvec3 from{-20.0, 0.0, 0.0};
	const glm::dvec3 along{1.0, 0.0, 0.0};
	const auto entities = [](const std::vector<Physics::CastHit> &hits)
	{
		std::vector<entt::entity> found;
		for (const Physics::CastHit &hit : hits)
			found.push_back(hit.entity);
		return found;
	};
	CHECK(
	    entities(physics.CastRayAll(kHalcyon, from, along, 50.0)) == std::vector{ghost, ship, barrier});
	CHECK(
	    entities(physics.CastRayAll(kHalcyon, from, along, 50.0, {.layer = "ship"})) ==
	    std::vector{ship, barrier});
	CHECK(
	    entities(physics.CastRayAll(kHalcyon, from, along, 50.0, {.layer = "ghost"})) ==
	    std::vector{barrier});
	CHECK(physics.CastRay(kHalcyon, from, along, 50.0, {.layer = "ship"})->entity == ship);
	CHECK(
	    physics
	        .CastShape(
	            kHalcyon, "crate", {from, glm::dquat{1.0, 0.0, 0.0, 0.0}}, along, 50.0,
	            {.layer = "ghost"})
	        ->entity == barrier);
	CHECK(physics.OverlapSphere(kHalcyon, {2.0, 0.0, 0.0}, 3.0) == std::vector{ghost, ship});
	CHECK(
	    physics.OverlapSphere(kHalcyon, {2.0, 0.0, 0.0}, 3.0, {.layer = "ship"}) == std::vector{ship});

	// The hull reaches the wall in no layer, which every layer meets.
	CHECK(
	    physics.Overlap(
	        kHalcyon, "hull", {{2.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}}, {.layer = "ghost"}) ==
	    std::vector{barrier});
	CHECK_THROWS_WITH_AS(
	    (void)physics.CastRay(kHalcyon, from, along, 50.0, {.layer = "phantom"}),
	    doctest::Contains("a cast as layer 'phantom', which is not defined"), std::invalid_argument);
	CHECK_THROWS_AS(
	    (void)physics.OverlapSphere(kHalcyon, {}, 1.0, {.layer = "phantom"}), std::invalid_argument);
}


TEST_CASE("Physics: bodies of one group never meet, and meet those of another")
{
	Fixture fixture;
	Body ball = Dynamic("ball");
	ball.restitution = 1.0f;
	const entt::entity left = fixture.Add(InGroup(ball, 7), {-5.0, 0.0, 0.0}, {10.0f, 0.0f, 0.0f});
	const entt::entity right = fixture.Add(InGroup(ball, 7), {5.0, 0.0, 0.0}, {-10.0f, 0.0f, 0.0f});
	const entt::entity seventh = fixture.Add(InGroup(ball, 7), {-5.0, 50.0, 0.0}, {10.0f, 0.0f, 0.0f});
	const entt::entity eighth = fixture.Add(InGroup(ball, 8), {5.0, 50.0, 0.0}, {-10.0f, 0.0f, 0.0f});
	const entt::entity loner = fixture.Add(ball, {5.0, 100.0, 0.0}, {-10.0f, 0.0f, 0.0f});
	const entt::entity grouped = fixture.Add(InGroup(ball, 7), {-5.0, 100.0, 0.0}, {10.0f, 0.0f, 0.0f});
	fixture.Run(1.0);
	CHECK(fixture.Linear(left).x == doctest::Approx(10.0));
	CHECK(fixture.Linear(right).x == doctest::Approx(-10.0));
	CHECK(fixture.Linear(seventh).x == doctest::Approx(-10.0).epsilon(0.02));
	CHECK(fixture.Linear(eighth).x == doctest::Approx(10.0).epsilon(0.02));
	CHECK(fixture.Linear(grouped).x == doctest::Approx(-10.0).epsilon(0.02));
	CHECK(fixture.Linear(loner).x == doctest::Approx(10.0).epsilon(0.02));

	fixture.Add(InGroup(ball, std::numeric_limits<std::uint32_t>::max()), {0.0, -50.0, 0.0});
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("which Jolt keeps for none"), std::invalid_argument);
}


TEST_CASE("Physics: layers are defined once, before any body, and each names only layers defined")
{
	Fixture fixture;
	Physics::PhysicsWorld &physics = fixture.physics;
	CHECK_THROWS_WITH_AS(
	    physics.DefineLayers({{"ship", {{"ship", "drone"}}}}),
	    doctest::Contains("layer 'ship' meets 'drone', which is not defined"), std::invalid_argument);
	CHECK_THROWS_WITH_AS(
	    physics.DefineLayers({{"", {}}}), doctest::Contains("a layer needs a name"),
	    std::invalid_argument);

	const entt::entity stray = fixture.Add(InLayer(Dynamic("ball"), "ship"), {0.0, 0.0, 0.0});
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("is in layer 'ship', which is not defined"),
	    std::invalid_argument);
	DefineGhostsAndShips(physics);
	CHECK_THROWS_AS(DefineGhostsAndShips(physics), std::logic_error);
	fixture.world.Step();
	CHECK(physics.BodyCount() == 1);

	// A change of layer makes the body again, in its new layer.
	fixture.world.Registry().get<Body>(stray).layer = "ghost";
	fixture.world.Step();
	CHECK(physics.OverlapSphere(kHalcyon, {0.0, 0.0, 0.0}, 1.0, {.layer = "ship"}).empty());

	Fixture late;
	late.Add(Dynamic("ball"), {0.0, 0.0, 0.0});
	late.world.Step();
	CHECK_THROWS_WITH_AS(
	    DefineGhostsAndShips(late.physics), doctest::Contains("before any body is made"),
	    std::logic_error);
}


TEST_CASE("Physics: layers are definitions, and a mod's layer meets the base game's by naming it")
{
	const std::filesystem::path directory = std::filesystem::temp_directory_path() /
	                                        ("jettison-physics-layers-" + std::to_string(getpid()));
	std::filesystem::remove_all(directory);
	std::filesystem::create_directories(directory / "base/layers");
	std::filesystem::create_directories(directory / "mod/layers");
	std::ofstream{directory / "base/layers/ship.yaml"} << "version: 1\nmeets: [ship]\n";
	std::ofstream{directory / "mod/layers/drone.yaml"} << "version: 1\nmeets: [ship]\n";

	Vfs::FileSystem fileSystem;
	fileSystem.AddMount({"base", directory / "base", false});
	fileSystem.AddMount({"mod", directory / "mod", false});
	Physics::RegisterTypes();
	Sim::DefinitionRegistry definitions{fileSystem};
	definitions.Load<Physics::LayerDefinition>("layers", Serialise::FormatVersion{1});
	REQUIRE(definitions.Find<Physics::LayerDefinition>("drone"));

	Fixture fixture;
	fixture.physics.DefineLayers(definitions);
	const entt::entity ship = fixture.Add(InLayer(Dynamic("ball"), "ship"), {0.0, 0.0, 0.0});
	const entt::entity drone = fixture.Add(InLayer(Dynamic("ball"), "drone"), {10.0, 0.0, 0.0});
	fixture.world.Step();

	// The drone names the ship, so they meet both ways; it does not name itself.
	CHECK(
	    fixture.physics.OverlapSphere(kHalcyon, {0.0, 0.0, 0.0}, 1.0, {.layer = "drone"}) ==
	    std::vector{ship});
	CHECK(
	    fixture.physics.OverlapSphere(kHalcyon, {10.0, 0.0, 0.0}, 1.0, {.layer = "ship"}) ==
	    std::vector{drone});
	CHECK(fixture.physics.OverlapSphere(kHalcyon, {10.0, 0.0, 0.0}, 1.0, {.layer = "drone"}).empty());
	std::filesystem::remove_all(directory);
}


TEST_CASE("Physics: layered and grouped bodies are saved, and a world without the layers refuses them")
{
	const auto populate = [](Fixture &fixture)
	{
		DefineGhostsAndShips(fixture.physics);
		fixture.Add(InLayer(Dynamic("ball"), "ghost"), {-5.0, 0.0, 0.0}, {10.0f, 0.0f, 0.0f});
		fixture.Add(InLayer(Dynamic("ball"), "ghost"), {5.0, 0.0, 0.0}, {-10.0f, 0.0f, 0.0f});
		fixture.Add(InGroup(Dynamic("ball"), 3), {-5.0, 50.0, 0.0}, {10.0f, 0.0f, 0.0f});
		fixture.Add(InGroup(Dynamic("ball"), 3), {5.0, 50.0, 0.0}, {-10.0f, 0.0f, 0.0f});
	};
	Fixture original;
	populate(original);
	original.Run(0.3);
	const std::vector<std::byte> save = original.world.Save();

	Fixture loaded;
	DefineGhostsAndShips(loaded.physics);
	loaded.world.Load(save);
	CHECK(loaded.world.StateHash() == original.world.StateHash());
	original.Run(0.5);
	loaded.Run(0.5);
	CHECK(loaded.world.StateHash() == original.world.StateHash());

	Fixture bare;
	CHECK_THROWS_WITH_AS(
	    bare.world.Load(save), doctest::Contains("is in layer 'ghost', which is not defined"),
	    Serialise::FormatError);
}
