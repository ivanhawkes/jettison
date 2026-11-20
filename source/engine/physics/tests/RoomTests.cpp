#include <jettison/physics/PhysicsWorld.h>

#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Physics as a game of no star system has it: a room with its own gravity and a lift, the game's
// frame model (Physics::FrameModel), and nothing of celestial's.
using namespace Jettison;
using Physics::Body;
using Physics::BodyKind;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kRoom = Sim::FrameId("Room");
const glm::dvec3 kGravity{0.0, -9.81, 0.0};
// The lift's speed along x, from the room's origin at the start.
constexpr double kLiftSpeed = 2.0;

class Room final : public Physics::FrameModel
{
  public:
	bool HasBody(std::string_view body) const override
	{
		return body == "lift";
	}


	Sim::Transform BodyAt(std::string_view /*body*/, double simSeconds) const override
	{
		++placed;
		return {{kLiftSpeed * simSeconds, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}};
	}


	std::function<glm::dvec3(const glm::dvec3 &)> GravityAt(double /*simSeconds*/) const override
	{
		return [](const glm::dvec3 &) { return kGravity; };
	}

	// How many times the lift has been placed.
	mutable int placed{0};
};


Physics::ShapeDefinition Box(glm::vec3 halfExtents)
{
	Physics::ShapeDefinition shape;
	shape.kind = Physics::ShapeKind::Box;
	shape.halfExtents = halfExtents;
	return shape;
}


struct Fixture
{
	Fixture() : physics{Setup(world)}
	{
	}


	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
		Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
		physics.DefineShape("floor", Box({20.0f, 0.5f, 20.0f}));
		physics.DefineShape("plate", Box({2.0f, 0.25f, 2.0f}));
		physics.DefineShape("crate", Box({1.0f, 1.0f, 1.0f}));
		physics.DefineFrame(kRoom, std::make_unique<Room>());
		return physics;
	}


	entt::entity Add(BodyKind kind, std::string shape, const glm::dvec3 &position)
	{
		entt::registry &registry = world.Registry();
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(
		    entity, Sim::Transform{position, glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InFrame>(entity, kRoom);
		Body body;
		body.kind = kind;
		body.shape = std::move(shape);
		body.mass = kind == BodyKind::Dynamic ? 50.0f : 0.0f;
		registry.emplace<Body>(entity, body);
		return entity;
	}


	// A floor whose top is at y = 0, and two crates dropped onto it, one above the other.
	void Stack()
	{
		floor = Add(BodyKind::Static, "floor", {0.0, -0.5, 0.0});
		lower = Add(BodyKind::Dynamic, "crate", {0.0, 4.0, 0.0});
		upper = Add(BodyKind::Dynamic, "crate", {0.0, 8.0, 0.0});
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


	bool Touches(entt::entity first, entt::entity second) const
	{
		const std::vector<entt::entity> touching = physics.Touching(first);
		return std::ranges::find(touching, second) != touching.end();
	}

	Sim::SimWorld world{kStep, 1};
	Physics::PhysicsWorld &physics;
	entt::entity floor{entt::null};
	entt::entity lower{entt::null};
	entt::entity upper{entt::null};
};
} // namespace


TEST_CASE("Physics: crates fall in a room of the game's own gravity and stack on its floor")
{
	Fixture fixture;
	fixture.Stack();
	CHECK(fixture.physics.Gravity(kRoom, {3.0, 1.0, -2.0}, 0.0) == kGravity);
	CHECK(fixture.physics.Gravity(Sim::FrameId("Cellar"), {}, 0.0) == glm::dvec3{0.0});

	fixture.Run(4.0);
	CHECK(fixture.Position(fixture.lower).y == doctest::Approx(1.0).epsilon(0.02));
	CHECK(fixture.Position(fixture.upper).y == doctest::Approx(3.0).epsilon(0.02));
	CHECK(fixture.Touches(fixture.floor, fixture.lower));
	CHECK(fixture.Touches(fixture.lower, fixture.upper));
	CHECK_FALSE(fixture.Touches(fixture.floor, fixture.upper));
	CHECK(fixture.physics.Frames() == std::vector{kRoom});
}


TEST_CASE("Physics: a plate on the room's rails carries a crate in its local frame with the lift")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity floor = fixture.Add(BodyKind::Static, "floor", {0.0, -10.0, 0.0});
	const entt::entity plate = fixture.Add(BodyKind::Kinematic, "plate", {0.0, 0.0, 0.0});
	registry.emplace<Physics::OnRails>(plate, "lift");
	registry.emplace<Physics::LocalFrame>(plate, "lift");
	const entt::entity crate = fixture.Add(BodyKind::Dynamic, "crate", {0.0, 1.3, 0.0});
	registry.emplace<Physics::LocalFrame>(crate, "lift");

	fixture.Run(2.0);
	CHECK(fixture.Position(plate).x == doctest::Approx(kLiftSpeed * 2.0));
	CHECK(fixture.Position(crate).x == doctest::Approx(kLiftSpeed * 2.0).epsilon(0.01));
	CHECK(fixture.Position(crate).y == doctest::Approx(1.25).epsilon(0.02));
	CHECK(fixture.Touches(plate, crate));
	CHECK(fixture.Position(floor).x == 0.0);
	// The room's own and the lift's.
	CHECK(fixture.physics.FrameCount(kRoom) == 2);
	CHECK(
	    fixture.physics.RailsPlace(kRoom, {"lift", {0.0, 1.0, 0.0}, {}}, 3.0).position ==
	    glm::dvec3{kLiftSpeed * 3.0, 1.0, 0.0});
	CHECK(fixture.physics.FrameOrigin(kRoom, "lift", 3.0) == glm::dvec3{kLiftSpeed * 3.0, 0.0, 0.0});
	CHECK(fixture.physics.FrameOrigin(kRoom, "", 3.0) == glm::dvec3{0.0});
}


TEST_CASE("Physics: a body is placed once a step, however many entities ride on it")
{
	Fixture fixture;
	auto pHall = std::make_unique<Room>();
	const Room &hall = *pHall;
	const std::uint64_t kHall = Sim::FrameId("Hall");
	fixture.physics.DefineFrame(kHall, std::move(pHall));
	entt::registry &registry = fixture.world.Registry();
	std::vector<entt::entity> plates;
	for (const double x : {-5.0, 0.0, 5.0})
	{
		plates.push_back(fixture.Add(BodyKind::Kinematic, "plate", {x, 0.0, 0.0}));
		registry.get<Sim::InFrame>(plates.back()).frame = kHall;
		registry.emplace<Physics::OnRails>(
		    plates.back(), Physics::OnRails{.body = "lift", .offset = {x, 0.0, 0.0}});
	}
	fixture.world.Step();

	const int before = hall.placed;
	fixture.world.Step();
	CHECK(hall.placed - before == 1);
	CHECK(fixture.Position(plates[2]) == glm::dvec3{5.0 + kLiftSpeed * 2.0 * kStep, 0.0, 0.0});
}


TEST_CASE("Physics: a frame's model is defined once, and rails it has no body for are refused")
{
	Fixture fixture;
	CHECK(fixture.physics.HasFrame(kRoom));
	CHECK_FALSE(fixture.physics.HasFrame(Sim::FrameId("Cellar")));
	CHECK_THROWS_WITH_AS(
	    fixture.physics.DefineFrame(kRoom, std::make_unique<Room>()),
	    doctest::Contains("is already defined"), std::invalid_argument);
	CHECK_THROWS_WITH_AS(
	    fixture.physics.DefineFrame(Sim::FrameId("Cellar"), nullptr),
	    doctest::Contains("is defined as nothing"), std::invalid_argument);
	CHECK_THROWS_WITH_AS(
	    (void)fixture.physics.RailsPlace(Sim::FrameId("Cellar"), {"lift", {}, {}}, 0.0),
	    doctest::Contains("is not defined"), std::invalid_argument);

	const entt::entity stairs = fixture.Add(BodyKind::Kinematic, "plate", {0.0, 0.0, 0.0});
	fixture.world.Registry().emplace<Physics::OnRails>(stairs, "stairs");
	const std::uint64_t before = fixture.world.StateHash();
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("has no body 'stairs'"), std::invalid_argument);
	CHECK(fixture.world.StateHash() == before);
}


TEST_CASE("Physics: a room saved part-way and loaded carries on exactly as the original")
{
	Fixture original;
	original.Stack();
	original.Run(1.0);
	const std::vector<std::byte> save = original.world.Save();

	Fixture loaded;
	loaded.world.Load(save);
	CHECK(loaded.physics.BodyCount() == 3);
	original.Run(2.0);
	loaded.Run(2.0);
	CHECK(loaded.world.Save() == original.world.Save());
}
