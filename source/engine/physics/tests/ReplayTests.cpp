#include <jettison/physics/PhysicsWorld.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <tuple>
#include <vector>

using namespace Jettison;
using Physics::Body;
using Physics::BodyKind;
using Physics::ShapeDefinition;
using Physics::ShapeKind;
using Physics::Velocity;
using Reflect::Attribute;

namespace
{
// A cargo bay in Halcyon, closed on every side, with a paddle turning in it and cargo of every
// shape thrown about it, and two crates meeting in Vesper; then the commands a session would give,
// changing all of it. Every kind of body, every kind of shape, and every way a body changes are in
// the run, so its state hash covers what physics saves.

// Turns a kinematic body about its Y axis, as the game's systems move a station.
struct Spin
{
	double rate{0.0};
};

struct Launch
{
	std::string shape;
	BodyKind kind{BodyKind::Dynamic};
	float mass{0.0f};
	glm::dvec3 position{0.0};
	glm::vec3 linear{0.0f};
	glm::vec3 angular{0.0f};
	double spin{0.0};
	std::uint64_t system{0};
};
struct Push
{
	entt::entity body{entt::null};
	glm::vec3 linear{0.0f};
};
struct Scrap
{
	entt::entity body{entt::null};
};
struct Refit
{
	entt::entity body{entt::null};
	float mass{0.0f};
};
struct Transfer
{
	entt::entity body{entt::null};
	std::uint64_t system{0};
	glm::dvec3 position{0.0};
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		RegisterType<Spin>("ReplayTests.Spin").Field<&Spin::rate>("rate", Attribute::Saved);
		RegisterType<Launch>("ReplayTests.Launch")
		    .Field<&Launch::shape>("shape", Attribute::Saved)
		    .Field<&Launch::kind>("kind", Attribute::Saved)
		    .Field<&Launch::mass>("mass", Attribute::Saved)
		    .Field<&Launch::position>("position", Attribute::Saved)
		    .Field<&Launch::linear>("linear", Attribute::Saved)
		    .Field<&Launch::angular>("angular", Attribute::Saved)
		    .Field<&Launch::spin>("spin", Attribute::Saved)
		    .Field<&Launch::system>("system", Attribute::Saved);
		RegisterType<Push>("ReplayTests.Push")
		    .Field<&Push::body>("body", Attribute::Saved)
		    .Field<&Push::linear>("linear", Attribute::Saved);
		RegisterType<Scrap>("ReplayTests.Scrap").Field<&Scrap::body>("body", Attribute::Saved);
		RegisterType<Refit>("ReplayTests.Refit")
		    .Field<&Refit::body>("body", Attribute::Saved)
		    .Field<&Refit::mass>("mass", Attribute::Saved);
		RegisterType<Transfer>("ReplayTests.Transfer")
		    .Field<&Transfer::body>("body", Attribute::Saved)
		    .Field<&Transfer::system>("system", Attribute::Saved)
		    .Field<&Transfer::position>("position", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

constexpr double kStep = 1.0 / 60.0;
constexpr std::uint64_t kSeed = 0x0b0d1e5;
constexpr std::uint64_t kSteps = 300;
const std::uint64_t kHalcyon = Sim::SystemId("Halcyon");
const std::uint64_t kVesper = Sim::SystemId("Vesper");
// The bay's half width, inside.
constexpr float kBay = 30.0f;

// A closed cube of twelve triangles, each facing in.
ShapeDefinition Bay()
{
	ShapeDefinition shape;
	shape.kind = ShapeKind::Mesh;
	for (const float x : {-kBay, kBay})
		for (const float y : {-kBay, kBay})
			for (const float z : {-kBay, kBay})
				shape.vertices.emplace_back(x, y, z);
	// Vertex index bits: 4 is x, 2 is y, 1 is z. Each face is wound counter-clockwise seen from
	// inside the cube.
	shape.indices = {0, 2, 3, 0, 3, 1, 4, 5, 7, 4, 7, 6, 0, 1, 5, 0, 5, 4,
	                 2, 6, 7, 2, 7, 3, 0, 4, 6, 0, 6, 2, 1, 3, 7, 1, 7, 5};
	return shape;
}

// The game's start-up, the same every run.
Physics::PhysicsWorld &Populate(Sim::SimWorld &world)
{
	RegisterTypes();
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Sim::InSystem>();
	world.RegisterComponent<Spin>();
	world.RegisterSystem(
	    {"spin",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Spin>().Writing<Sim::Transform>(),
	     {},
	     [](entt::registry &registry, double step)
	     {
		     for (auto [entity, spin, transform] : registry.view<const Spin, Sim::Transform>().each())
			     transform.orientation = glm::normalize(
			         transform.orientation *
			         glm::angleAxis(spin.rate * step, glm::dvec3{0.0, 1.0, 0.0}));
	     }});

	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
	ShapeDefinition shape;
	physics.DefineShape("bay", Bay());
	shape.kind = ShapeKind::Box;
	shape.halfExtents = {6.0f, 0.5f, 1.0f};
	physics.DefineShape("paddle", shape);
	shape.halfExtents = {1.0f, 0.75f, 0.5f};
	physics.DefineShape("crate", shape);
	shape.kind = ShapeKind::Sphere;
	shape.radius = 1.25f;
	physics.DefineShape("ball", shape);
	shape.kind = ShapeKind::Capsule;
	shape.radius = 0.6f;
	shape.halfHeight = 1.4f;
	physics.DefineShape("capsule", shape);
	shape.kind = ShapeKind::Cylinder;
	shape.radius = 1.0f;
	shape.halfHeight = 0.8f;
	physics.DefineShape("drum", shape);
	shape.kind = ShapeKind::ConvexHull;
	shape.points = {
	    {0.0f, 2.0f, 0.0f},
	    {1.5f, -1.0f, 0.0f},
	    {-1.0f, -1.0f, 1.2f},
	    {-1.0f, -1.0f, -1.2f},
	    {0.3f, 0.2f, 0.9f}};
	physics.DefineShape("shard", shape);

	world.RegisterCommand<Launch>(
	    [](Sim::SimWorld &target, const Launch &command)
	    {
		    entt::registry &registry = target.Registry();
		    const entt::entity entity = registry.create();
		    registry.emplace<Sim::Transform>(entity, Sim::Transform{command.position});
		    registry.emplace<Sim::InSystem>(entity, command.system);
		    Body body;
		    body.kind = command.kind;
		    body.shape = command.shape;
		    body.mass = command.mass;
		    body.restitution = 0.6f;
		    body.friction = 0.4f;
		    registry.emplace<Body>(entity, body);
		    if (command.kind == BodyKind::Dynamic)
			    registry.emplace<Velocity>(entity, command.linear, command.angular);
		    if (command.spin != 0.0)
			    registry.emplace<Spin>(entity, command.spin);
	    });
	world.RegisterCommand<Push>(
	    [](Sim::SimWorld &target, const Push &command)
	    {
		    if (target.Registry().valid(command.body))
			    target.Registry().get<Velocity>(command.body).linear += command.linear;
	    });
	world.RegisterCommand<Scrap>(
	    [](Sim::SimWorld &target, const Scrap &command)
	    {
		    if (target.Registry().valid(command.body))
			    target.Registry().destroy(command.body);
	    });
	world.RegisterCommand<Refit>(
	    [](Sim::SimWorld &target, const Refit &command)
	    {
		    if (target.Registry().valid(command.body))
			    target.Registry().get<Body>(command.body).mass = command.mass;
	    });
	world.RegisterCommand<Transfer>(
	    [](Sim::SimWorld &target, const Transfer &command)
	    {
		    if (!target.Registry().valid(command.body))
			    return;
		    target.Registry().get<Sim::InSystem>(command.body).system = command.system;
		    target.Registry().get<Sim::Transform>(command.body).position = command.position;
	    });
	return physics;
}

entt::entity Id(std::uint32_t identifier)
{
	return entt::entity{identifier};
}

void Feed(Sim::SimWorld &world, std::uint64_t step)
{
	Sim::CommandQueue &commands = world.Commands();
	switch (step)
	{
		case 0:
		{
			// 0: the bay. 1: the paddle, turning at half a turn a second.
			commands.Push(
			    Launch{"bay", BodyKind::Static, 0.0f, {0.0, 0.0, 0.0}, {}, {}, 0.0, kHalcyon});
			commands.Push(
			    Launch{
			        "paddle", BodyKind::Kinematic, 0.0f, {0.0, -10.0, 0.0}, {}, {}, 3.14159, kHalcyon});
			// 2 to 9: cargo, thrown.
			constexpr std::array kCargo{"crate", "ball",  "capsule", "drum",
			                            "shard", "crate", "ball",    "shard"};
			for (std::size_t index = 0; index < kCargo.size(); ++index)
			{
				const auto offset = static_cast<double>(index) - 3.5;
				const auto speed = static_cast<float>(index);
				commands.Push(
				    Launch{
				        kCargo[index],
				        BodyKind::Dynamic,
				        200.0f + 50.0f * speed,
				        {offset * 6.0, 5.0 + offset, -offset * 2.0},
				        {12.0f - 3.0f * speed, -8.0f + speed, 4.0f * (speed - 4.0f)},
				        {0.5f * speed, 1.0f, -0.25f * speed},
				        0.0,
				        kHalcyon});
			}
			// 10 and 11: two crates meeting in Vesper, at the coordinates the bay has in Halcyon.
			commands.Push(
			    Launch{
			        "crate",
			        BodyKind::Dynamic,
			        500.0f,
			        {-10.0, 0.0, 0.0},
			        {6.0f, 0.0f, 0.0f},
			        {},
			        0.0,
			        kVesper});
			commands.Push(
			    Launch{
			        "crate",
			        BodyKind::Dynamic,
			        500.0f,
			        {10.0, 0.2, 0.0},
			        {-6.0f, 0.0f, 0.0f},
			        {},
			        0.0,
			        kVesper});
			break;
		}
		case 40: commands.Push(Push{Id(3), {0.0f, 25.0f, 0.0f}}); break;
		case 90: commands.Push(Scrap{Id(5)}); break;
		case 120:
			// The first takes the scrapped entity's index, with its next version.
			commands.Push(
			    Launch{
			        "drum",
			        BodyKind::Dynamic,
			        300.0f,
			        {0.0, 20.0, 0.0},
			        {0.0f, -30.0f, 2.0f},
			        {3.0f, 0.0f, 0.0f},
			        0.0,
			        kHalcyon});
			commands.Push(
			    Launch{
			        "ball",
			        BodyKind::Dynamic,
			        120.0f,
			        {20.0, 0.0, 20.0},
			        {-15.0f, 1.0f, -15.0f},
			        {},
			        0.0,
			        kHalcyon});
			commands.Push(
			    Launch{
			        "shard",
			        BodyKind::Dynamic,
			        90.0f,
			        {-20.0, 15.0, 5.0},
			        {9.0f, -9.0f, 0.0f},
			        {0.0f, 4.0f, 0.0f},
			        0.0,
			        kHalcyon});
			break;
		case 150: commands.Push(Refit{Id(4), 2000.0f}); break;
		case 200: commands.Push(Transfer{Id(6), kVesper, {0.0, 8.0, 0.0}}); break;
		case 250: commands.Push(Transfer{Id(7), Sim::kNoSystem, {0.0, 0.0, 0.0}}); break;
		default: break;
	}
}

void Run(Sim::SimWorld &world, std::uint64_t until)
{
	while (world.Clock().Step() < until)
	{
		Feed(world, world.Clock().Step());
		world.Step();
	}
}
} // namespace

TEST_CASE("Physics replay: bodies collide, and the same commands give the same state in every build")
{
	Sim::SimWorld first{kStep, kSeed};
	Sim::SimWorld second{kStep, kSeed};
	const Physics::PhysicsWorld &physics = Populate(first);
	Populate(second);
	Run(first, kSteps);
	Run(second, kSteps);
	CHECK(first.Save() == second.Save());

	// The bay holds what was thrown in it: every body in Halcyon is inside it, having hit its walls
	// many times over five seconds at up to 30 m/s.
	const entt::registry &registry = first.Registry();
	int inside = 0;
	for (auto [entity, body, transform, system] :
	     registry.view<const Body, const Sim::Transform, const Sim::InSystem>().each())
	{
		if (system.system != kHalcyon || body.kind != BodyKind::Dynamic)
			continue;
		CAPTURE(entt::to_integral(entity));
		CHECK(glm::all(glm::lessThan(glm::abs(transform.position), glm::dvec3{kBay})));
		++inside;
	}
	CHECK(inside == 8);
	CHECK(physics.BodyCount(kHalcyon) == 10);
	CHECK(physics.BodyCount(kVesper) == 3);
	// Vesper's crates met and parted, at the coordinates where the bay's walls are in Halcyon.
	CHECK(registry.get<Velocity>(Id(10)).linear.x < 0.0f);
	CHECK(registry.get<Velocity>(Id(11)).linear.x > 0.0f);

	// Recorded from a Debug build and checked by the Release suite and under the sanitisers too: a
	// change here is a change to physics' results or to what a save holds, and either is a
	// decision, not drift.
	CHECK(first.StateHash() == 0xf43a6412c4576d3eull);
}

TEST_CASE("Physics replay: a world saved part-way and loaded carries on exactly as the original")
{
	Sim::SimWorld original{kStep, kSeed};
	Populate(original);
	Run(original, 130);
	// Commands still queued are part of the save.
	Feed(original, 150);
	const std::vector<std::byte> save = original.Save();

	Sim::SimWorld loaded{kStep, 1};
	const Physics::PhysicsWorld &physics = Populate(loaded);
	loaded.Load(save);
	CHECK(loaded.Save() == save);
	CHECK(physics.BodyCount() == 14);

	Run(original, kSteps);
	Run(loaded, kSteps);
	CHECK(loaded.Save() == original.Save());
}

TEST_CASE("Physics replay: the recorded commands replay a session from its start")
{
	Sim::SimWorld session{kStep, kSeed};
	Populate(session);
	session.SetRecording(true);
	Run(session, kSteps);
	const std::vector<Sim::AppliedCommand> &recorded = session.Recorded();
	REQUIRE(recorded.size() == 20);

	Sim::SimWorld replay{kStep, kSeed};
	Populate(replay);
	std::size_t next = 0;
	while (replay.Clock().Step() < kSteps)
	{
		for (; next < recorded.size() && recorded[next].step == replay.Clock().Step(); ++next)
			replay.Commands().Push(recorded[next].command);
		replay.Step();
	}
	CHECK(replay.StateHash() == session.StateHash());
}

TEST_CASE("Physics replay: a save naming a shape this world does not define is refused")
{
	Sim::SimWorld original{kStep, kSeed};
	Populate(original);
	Run(original, 10);
	const std::vector<std::byte> save = original.Save();

	Sim::SimWorld bare{kStep, kSeed};
	RegisterTypes();
	bare.RegisterComponent<Sim::Transform>();
	bare.RegisterComponent<Sim::InSystem>();
	bare.RegisterComponent<Spin>();
	Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(bare);
	const std::uint64_t before = bare.StateHash();
	CHECK_THROWS_WITH_AS(
	    bare.Load(save), doctest::Contains("cannot be a body: it names shape"), Serialise::FormatError);
	CHECK(bare.StateHash() == before);
	CHECK(physics.BodyCount() == 0);

	// Cut short anywhere, it is refused, and the world it was loaded into is as it was.
	Sim::SimWorld loaded{kStep, kSeed};
	Populate(loaded);
	Run(loaded, 5);
	const std::uint64_t ran = loaded.StateHash();
	for (const std::size_t size : {save.size() / 3, save.size() / 2, save.size() - 40, save.size() - 1})
	{
		CAPTURE(size);
		CHECK_THROWS_AS(loaded.Load(std::span{save}.first(size)), Serialise::FormatError);
		CHECK(loaded.StateHash() == ran);
	}
}

TEST_CASE("Physics replay: a save of layout 3, before Jolt's version was saved, keeps Jolt's state")
{
	// Written by commit 8177358 from the run below at step 130, with step 150's command queued: Jolt
	// 5.6.0, as every save of layout 3 was.
	std::ifstream file{
	    std::filesystem::path{JETTISON_PHYSICS_TEST_DATA} / "replay-layout3.save", std::ios::binary};
	REQUIRE(file);
	const std::vector<char> text{
	    std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
	const auto *bytes = reinterpret_cast<const std::byte *>(text.data());
	const std::vector<std::byte> fixture{bytes, bytes + text.size()};

	Sim::SimWorld loaded{kStep, 1};
	const Physics::PhysicsWorld &physics = Populate(loaded);
	loaded.Load(fixture);
	CHECK(loaded.Clock().Step() == 130);
	CHECK(physics.BodyCount() == 14);

	Sim::SimWorld original{kStep, kSeed};
	Populate(original);
	Run(original, 130);
	Feed(original, 150);
	CHECK(loaded.Save() == original.Save());
	Run(original, kSteps);
	Run(loaded, kSteps);
	CHECK(loaded.Save() == original.Save());
}

TEST_CASE("Physics replay: a save of another Jolt is made again from its records, and steps on")
{
	// Saved at step 180, as three pairs touch: a ball and a shard against the bay's walls, and the
	// drum launched at step 120 against the turning paddle.
	Sim::SimWorld original{kStep, kSeed};
	Physics::PhysicsWorld &saving = Populate(original);
	Run(original, 180);
	REQUIRE(saving.Touching(Id(0)).size() == 2);
	REQUIRE(saving.Touching(Id(1)).size() == 1);
	saving.SaveAsJoltVersion({5, 5, 0, Physics::BuiltJoltVersion().features});
	const std::vector<std::byte> save = original.Save();
	saving.SaveAsJoltVersion(Physics::BuiltJoltVersion());

	Sim::SimWorld loaded{kStep, 1};
	const Physics::PhysicsWorld &physics = Populate(loaded);
	loaded.Load(save);
	CHECK(physics.BodyCount() == 14);
	for (const entt::entity entity : original.Registry().view<const Body>())
	{
		CAPTURE(entt::to_integral(entity));
		CHECK(physics.Touching(entity) == saving.Touching(entity));
	}

	// Without the contacts Jolt warm-starts from it is not the world saved, but near it: 2 nm out
	// after a step, 57 µm by step 300 (measured), the same pairs beginning and ending to touch in
	// each step, though where and how hard they meet is as near and no nearer.
	double farthest = 0.0;
	for (std::uint64_t step = 181; step <= kSteps; ++step)
	{
		Run(original, step);
		Run(loaded, step);
		CAPTURE(step);
		const auto pairs = [](const std::vector<Physics::ContactEvent> &contacts)
		{
			std::vector<std::tuple<Physics::ContactPhase, entt::entity, entt::entity>> pairs;
			pairs.reserve(contacts.size());
			for (const Physics::ContactEvent &contact : contacts)
				pairs.emplace_back(contact.phase, contact.first, contact.second);
			return pairs;
		};
		CHECK(pairs(physics.Contacts()) == pairs(saving.Contacts()));
		for (auto [entity, body, transform] :
		     original.Registry().view<const Body, const Sim::Transform>().each())
			if (body.kind == BodyKind::Dynamic)
				farthest = std::max(
				    farthest,
				    glm::distance(
				        transform.position, loaded.Registry().get<Sim::Transform>(entity).position));
	}
	CHECK(farthest > 0.0);
	CHECK(farthest < 1.0e-4);

	// Saved again, it is this build's Jolt's, and a world loaded from it carries on exactly.
	const std::vector<std::byte> again = loaded.Save();
	Sim::SimWorld reloaded{kStep, 1};
	Populate(reloaded);
	reloaded.Load(again);
	CHECK(reloaded.Save() == again);
	Run(loaded, kSteps + 60);
	Run(reloaded, kSteps + 60);
	CHECK(reloaded.Save() == loaded.Save());
}
