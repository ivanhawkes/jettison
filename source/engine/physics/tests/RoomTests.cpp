#include <jettison/physics/PhysicsWorld.h>

#include <jettison/physics/Flight.h>
#include <jettison/physics/SnapshotPicker.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
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


// A turntable 1 km along x, drifting at 1 m/s and turning at half a radian a second, as a gate's ring
// turns after its planet; no gravity.
const std::uint64_t kCarousel = Sim::FrameId("Carousel");

class Carousel final : public Physics::FrameModel
{
  public:
	bool HasBody(std::string_view body) const override
	{
		return body == "turntable";
	}


	Sim::Transform BodyAt(std::string_view /*body*/, double simSeconds) const override
	{
		return {
		    {1000.0 + simSeconds, 0.0, 0.0},
		    glm::angleAxis(0.5 * simSeconds, glm::dvec3{0.0, 1.0, 0.0})};
	}
};


// A plate on the lift and a sensor on the turntable, each in its body's local frame, and a floor.
void Furnish(Fixture &fixture)
{
	fixture.physics.DefineFrame(kCarousel, std::make_unique<Carousel>());
	entt::registry &registry = fixture.world.Registry();
	fixture.floor = fixture.Add(BodyKind::Static, "floor", {0.0, -0.5, 0.0});
	const entt::entity plate = fixture.Add(BodyKind::Kinematic, "plate", {0.0, 0.0, 0.0});
	registry.emplace<Physics::OnRails>(
	    plate, Physics::OnRails{.body = "lift", .offset = {3.0, 1.0, 0.0}});
	registry.emplace<Physics::LocalFrame>(plate, "lift");
	const entt::entity ring = registry.create();
	const Physics::OnRails rails{.body = "turntable", .offset = {5.0, 0.0, 0.0}};
	registry.emplace<Sim::Transform>(ring, fixture.physics.RailsPlace(kCarousel, rails, 0.0));
	registry.emplace<Sim::InFrame>(ring, kCarousel);
	registry.emplace<Body>(ring, Body{.kind = BodyKind::Kinematic, .shape = "crate", .sensor = true});
	registry.emplace<Physics::OnRails>(ring, rails);
	registry.emplace<Physics::LocalFrame>(ring, "turntable");

	// A static body the lift's rails carry in the room's own frame: placed by each step, and by a skip.
	const entt::entity shelf = fixture.Add(BodyKind::Static, "plate", {0.0, 0.0, 0.0});
	registry.emplace<Physics::OnRails>(
	    shelf, Physics::OnRails{.body = "lift", .offset = {0.0, 3.0, 0.0}});
}


TEST_CASE("Physics: steps with nothing dynamic are passed over to the state stepping them leaves")
{
	Fixture forwarded;
	Fixture stepped;
	Furnish(forwarded);
	Furnish(stepped);

	// Three steps to rest (a body made, then two with nothing awake), then the rest in one.
	constexpr std::uint64_t kEnd = 20000;
	std::vector<std::uint64_t> advanced;
	while (forwarded.world.Clock().Step() < kEnd)
	{
		advanced.push_back(forwarded.world.FastForward(kEnd));
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}
	CHECK(advanced == std::vector<std::uint64_t>{1, 1, 1, kEnd - 3});

	// What a cast meets is where stepping left it: the shelf, 2 m/s along with the lift.
	const auto shelfHit = [](const Fixture &fixture)
	{
		const glm::dvec3 lift{kLiftSpeed * fixture.world.Clock().Now(), 3.0, 0.0};
		return fixture.physics.CastRay(
		    kRoom, lift + glm::dvec3{0.0, 10.0, 0.0}, {0.0, -1.0, 0.0}, 20.0);
	};
	REQUIRE(shelfHit(forwarded));
	CHECK(shelfHit(forwarded)->distance == shelfHit(stepped)->distance);

	// Held, the turntable's sensor still turns with it, as its velocity says.
	entt::entity ring = entt::null;
	for (const auto [entity, local] :
	     forwarded.world.Registry().view<const Physics::LocalFrame>().each())
		if (local.body == "turntable")
			ring = entity;
	CHECK(
	    glm::length(forwarded.world.Registry().get<Physics::Velocity>(ring).angular) ==
	    doctest::Approx(0.5));

	// Something dynamic in each frame: a crate falling onto the lift's plate, and one through the
	// sensor. Stepped from there, both worlds go on alike, and nothing is passed over meanwhile.
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		entt::registry &registry = fixture->world.Registry();
		const entt::entity crate = fixture->Add(BodyKind::Dynamic, "crate", {0.0, 6.0, 0.0});
		registry.emplace<Physics::LocalFrame>(crate, "lift");
		const entt::entity through = registry.create();
		registry.emplace<Sim::Transform>(
		    through, Sim::Transform{
		                 fixture->physics
		                     .RailsPlace(
		                         kCarousel, {.body = "turntable", .offset = {5.0, 4.0, 0.0}},
		                         fixture->world.Clock().Now())
		                     .position,
		                 glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InFrame>(through, kCarousel);
		registry.emplace<Physics::LocalFrame>(through, "turntable");
		registry.emplace<Physics::Velocity>(through, Physics::Velocity{.linear = {0.0f, -5.0f, 0.0f}});
		registry.emplace<Body>(
		    through, Body{.kind = BodyKind::Dynamic, .shape = "crate", .mass = 50.0f});
	}
	bool sensed = false;
	for (int step = 0; step < 240; ++step)
	{
		CHECK(forwarded.world.FastForward(kEnd + 1000) == 1);
		stepped.world.Step();
		sensed = sensed || std::ranges::any_of(
		                       forwarded.physics.Contacts(),
		                       [&](const Physics::ContactEvent &contact) { return contact.sensor; });
	}
	CHECK(forwarded.world.Save() == stepped.world.Save());
	CHECK(sensed);
}


TEST_CASE("Physics: rails changed after the physics step are a step to run, not to pass over")
{
	Fixture forwarded;
	Fixture stepped;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		Furnish(*fixture);

		// A game's system after physics moves the plate along the lift's rails at step 5.
		Sim::SimWorld &world = fixture->world;
		world.RegisterSystem(
		    {"shift",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<>().Writing<Physics::OnRails>(),
		     {std::string{Physics::PhysicsWorld::kSystemName}},
		     [&world](entt::registry &registry, double)
		     {
			     if (world.Clock().Step() == 5)
				     for (const auto [entity, rails, local] :
				          registry.view<Physics::OnRails, const Physics::LocalFrame>().each())
					     if (local.body == "lift")
						     rails.offset.x += 10.0;
		     }},
		    {.next = [](const Sim::SimWorld &target)
		     {
			     return target.Clock().Step() <= 5 ? std::uint64_t{5}
			                                       : std::numeric_limits<std::uint64_t>::max();
		     }});
	}
	for (int step = 0; step < 6; ++step)
	{
		forwarded.world.FastForward(6);
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
	}
	REQUIRE(forwarded.world.Clock().Step() == 6);

	// The step after the change places the plate by its new rails, its velocity the jump's; then the
	// lift's rails carry it, quiet again.
	CHECK(forwarded.world.FastForward(1000) == 1);
	stepped.world.Step();
	CHECK(forwarded.world.Save() == stepped.world.Save());
	CHECK(forwarded.world.FastForward(1000) > 1);
	while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
		stepped.world.Step();
	CHECK(forwarded.world.Save() == stepped.world.Save());
}


TEST_CASE("Physics: flight's change to a kinematic body in no frame is a step to run, not to pass over")
{
	Fixture forwarded;
	Fixture stepped;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		entt::registry &registry = fixture->world.Registry();
		const entt::entity ship = fixture->Add(BodyKind::Kinematic, "crate", {0.0, 0.0, 0.0});
		registry.get<Sim::InFrame>(ship).frame = Sim::kNoFrame;
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::Handling>(ship);
		registry.emplace<Physics::FlightControls>(
		    ship, Physics::FlightControls{.linear = {1.0f, 0.0f, 0.0f}, .assist = false});
	}
	for (int call = 0; call < 20; ++call)
	{
		CHECK(forwarded.world.FastForward(1000) == 1);
		stepped.world.Step();
	}
	CHECK(forwarded.world.Save() == stepped.world.Save());
}


TEST_CASE("Physics: a ship flown against a reference on rails is flown every step")
{
	Fixture forwarded;
	Fixture stepped;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		Furnish(*fixture);
		Sim::SimWorld &world = fixture->world;
		entt::registry &registry = world.Registry();

		// Held to the carousel's turning sensor, whose velocity turns with it: flown, with no body.
		entt::entity ring = entt::null;
		for (const auto [entity, local] : registry.view<const Physics::LocalFrame>().each())
			if (local.body == "turntable")
				ring = entity;
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(ship);
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::Handling>(ship);
		registry.emplace<Physics::FlightControls>(ship, Physics::FlightControls{.reference = ring});

		// Its velocity the ring's at step 5, after physics: at that moment the flight model would
		// change nothing, but the ring's goes on turning.
		world.RegisterSystem(
		    {"match",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<>().Writing<Physics::Velocity>(),
		     {std::string{Physics::PhysicsWorld::kSystemName}},
		     [&world, ship, ring](entt::registry &target, double)
		     {
			     if (world.Clock().Step() == 5)
				     target.get<Physics::Velocity>(ship).linear =
				         target.get<Physics::Velocity>(ring).linear;
		     }},
		    {.next = [](const Sim::SimWorld &target)
		     {
			     return target.Clock().Step() <= 5 ? std::uint64_t{5}
			                                       : std::numeric_limits<std::uint64_t>::max();
		     }});
	}
	while (forwarded.world.Clock().Step() < 200)
	{
		forwarded.world.FastForward(200);
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}
}


// A spindle at the origin turning at a radian a second, square to the room as the third step ends.
const std::uint64_t kSpindle = Sim::FrameId("Spindle");

class Spindle final : public Physics::FrameModel
{
  public:
	bool HasBody(std::string_view body) const override
	{
		return body == "spindle";
	}


	Sim::Transform BodyAt(std::string_view /*body*/, double simSeconds) const override
	{
		return {{0.0, 0.0, 0.0}, glm::angleAxis(simSeconds - 3.0 * kStep, glm::dvec3{0.0, 1.0, 0.0})};
	}
};


TEST_CASE("Physics: a ship its rails turn is flown every step")
{
	Fixture forwarded;
	Fixture stepped;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		fixture->physics.DefineFrame(kSpindle, std::make_unique<Spindle>());
		Sim::SimWorld &world = fixture->world;
		entt::registry &registry = world.Registry();

		// Flown full ahead to starboard at its top speed, with no body, on the spindle's rails.
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(ship);
		registry.emplace<Sim::InFrame>(ship, kSpindle);
		registry.emplace<Physics::OnRails>(ship, Physics::OnRails{.body = "spindle"});
		registry.emplace<Physics::Velocity>(ship);
		registry.emplace<Physics::Handling>(ship, Physics::Handling{.maxSpeed = 1.0f});
		registry.emplace<Physics::FlightControls>(
		    ship, Physics::FlightControls{.linear = {1.0f, 0.0f, 0.0f}});

		// At that speed as the third step ends, square to the room: the flight model's next change
		// is nothing, but the rails go on turning what it is held to.
		world.RegisterSystem(
		    {"match",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<>().Writing<Physics::Velocity>(),
		     {std::string{Physics::PhysicsWorld::kSystemName}},
		     [&world, ship](entt::registry &target, double)
		     {
			     if (world.Clock().Step() == 2)
				     target.get<Physics::Velocity>(ship) =
				         Physics::Velocity{.linear = {1.0f, 0.0f, 0.0f}};
		     }},
		    {.next = [](const Sim::SimWorld &target)
		     {
			     return target.Clock().Step() <= 2 ? std::uint64_t{2}
			                                       : std::numeric_limits<std::uint64_t>::max();
		     }});
	}
	while (forwarded.world.Clock().Step() < 100)
	{
		forwarded.world.FastForward(100);
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}
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


TEST_CASE("Physics: a pick against the snapshots hits what a cast does, where the frame draws it")
{
	Fixture fixture;
	fixture.world.ShowInSnapshots<Body>();
	fixture.Stack();
	fixture.Run(0.5);
	const Sim::Snapshot previous = Sim::Capture(fixture.world);
	fixture.world.Step();
	const Sim::Snapshot current = Sim::Capture(fixture.world);
	const Physics::SnapshotPicker picker{fixture.physics};
	const glm::dvec3 down{0.0, -1.0, 0.0};
	const glm::dvec3 above{0.0, 20.0, 0.0};

	// At the newest snapshot, the upper crate, as the world's own cast finds it.
	const std::optional<Physics::CastHit> picked =
	    picker.CastRay(previous, current, 1.0, kRoom, above, down, 50.0);
	const std::optional<Physics::CastHit> cast = fixture.physics.CastRay(kRoom, above, down, 50.0);
	REQUIRE(picked);
	REQUIRE(cast);
	CHECK(picked->entity == fixture.upper);
	CHECK(picked->entity == cast->entity);
	CHECK(picked->distance == doctest::Approx(cast->distance).epsilon(1e-6));
	CHECK(picked->normal.y == doctest::Approx(1.0f));

	// Between the two, where the crate is drawn: its top as Sim::Interpolate places it.
	const std::optional<Sim::Transform> drawn = Sim::Interpolate(previous, current, fixture.upper, 0.5);
	REQUIRE(drawn);
	const std::optional<Physics::CastHit> between =
	    picker.CastRay(previous, current, 0.5, kRoom, above, down, 50.0);
	REQUIRE(between);
	CHECK(between->distance == doctest::Approx(above.y - (drawn->position.y + 1.0)).epsilon(1e-5));

	// Beside the crates, the floor, which a pick of what moves leaves out.
	const glm::dvec3 beside{10.0, 20.0, 0.0};
	const std::optional<Physics::CastHit> floor =
	    picker.CastRay(previous, current, 1.0, kRoom, beside, down, 50.0);
	REQUIRE(floor);
	CHECK(floor->entity == fixture.floor);
	CHECK_FALSE(picker.CastRay(previous, current, 1.0, kRoom, beside, down, 50.0, {.statics = false}));

	// The one it passes through, and a ray of nothing.
	const std::optional<Physics::CastHit> past =
	    picker.CastRay(previous, current, 1.0, kRoom, above, down, 50.0, {.ignore = fixture.upper});
	REQUIRE(past);
	CHECK(past->entity == fixture.lower);
	CHECK_FALSE(picker.CastRay(previous, current, 1.0, kRoom, above, {0.0, 0.0, 0.0}, 50.0));
	CHECK_FALSE(picker.CastRay(previous, current, 1.0, Sim::FrameId("Elsewhere"), above, down, 50.0));

	// A picker of its own shapes passes over a body whose shape it has not been given.
	Physics::SnapshotPicker own;
	own.DefineShape("floor", Box({20.0f, 0.5f, 20.0f}));
	const std::optional<Physics::CastHit> ownHit =
	    own.CastRay(previous, current, 1.0, kRoom, above, down, 50.0);
	REQUIRE(ownHit);
	CHECK(ownHit->entity == fixture.floor);
}


Physics::CastOptions Ignoring(entt::entity entity)
{
	Physics::CastOptions options;
	options.ignore = entity;
	return options;
}


TEST_CASE("Physics: casts that return every hit, and overlaps, find the stack in order")
{
	Fixture fixture;
	fixture.Stack();
	fixture.Run(4.0);
	const glm::dvec3 down{0.0, -1.0, 0.0};
	const glm::dvec3 above{0.0, 20.0, 0.0};
	const std::vector<entt::entity> stack{fixture.upper, fixture.lower, fixture.floor};
	const auto entities = [](const std::vector<Physics::CastHit> &hits)
	{
		std::vector<entt::entity> found;
		found.reserve(hits.size());
		for (const Physics::CastHit &hit : hits)
			found.push_back(hit.entity);
		return found;
	};

	// A ray down through the stack: the upper crate, the lower and the floor, the first as CastRay.
	const std::vector<Physics::CastHit> ray = fixture.physics.CastRayAll(kRoom, above, down, 50.0);
	const std::optional<Physics::CastHit> first = fixture.physics.CastRay(kRoom, above, down, 50.0);
	REQUIRE(first);
	CHECK(entities(ray) == stack);
	CHECK(ray.front().entity == first->entity);
	CHECK(ray.front().distance == first->distance);
	CHECK(ray.front().point == first->point);
	CHECK(ray.front().normal == first->normal);
	CHECK(ray[1].distance == doctest::Approx(18.0).epsilon(0.01));
	CHECK(ray[2].distance == doctest::Approx(20.0).epsilon(0.01));
	CHECK(
	    entities(fixture.physics.CastRayAll(kRoom, above, down, 50.0, Ignoring(fixture.lower))) ==
	    std::vector{fixture.upper, fixture.floor});
	CHECK(
	    entities(fixture.physics.CastRayAll(kRoom, above, down, 19.0)) ==
	    std::vector{fixture.upper, fixture.lower});
	CHECK(fixture.physics.CastRayAll(kRoom, above, {0.0, 1.0, 0.0}, 50.0).empty());
	CHECK(fixture.physics.CastRayAll(Sim::FrameId("Elsewhere"), above, down, 50.0).empty());
	CHECK_THROWS_AS(
	    (void)fixture.physics.CastRayAll(kRoom, above, {0.0, 0.0, 0.0}, 50.0), std::invalid_argument);
	CHECK_THROWS_AS((void)fixture.physics.CastRayAll(kRoom, above, down, 0.0), std::invalid_argument);

	// A plate swept down the same way meets the same bodies, each once.
	const Sim::Transform start{above, glm::dquat{1.0, 0.0, 0.0, 0.0}};
	const std::vector<Physics::CastHit> sweep =
	    fixture.physics.CastShapeAll(kRoom, "plate", start, down, 50.0);
	const std::optional<Physics::CastHit> swept =
	    fixture.physics.CastShape(kRoom, "plate", start, down, 50.0);
	REQUIRE(swept);
	CHECK(entities(sweep) == stack);
	CHECK(sweep.front().entity == swept->entity);
	CHECK(sweep.front().distance == swept->distance);
	CHECK(sweep.front().point == swept->point);
	CHECK(
	    entities(
	        fixture.physics.CastShapeAll(kRoom, "plate", start, down, 50.0, Ignoring(fixture.upper))) ==
	    std::vector{fixture.lower, fixture.floor});
	CHECK_THROWS_AS(
	    (void)fixture.physics.CastShapeAll(kRoom, "nothing", start, down, 50.0), std::invalid_argument);
	CHECK_THROWS_AS(
	    (void)fixture.physics.CastShapeAll(kRoom, "plate", start, {}, 50.0), std::invalid_argument);

	// A crate's volume placed across the join of the two crates overlaps both and not the floor.
	const Sim::Transform across{{0.0, 2.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}};
	CHECK(fixture.physics.Overlap(kRoom, "crate", across) == std::vector{fixture.lower, fixture.upper});
	CHECK(
	    fixture.physics.Overlap(kRoom, "crate", across, Ignoring(fixture.lower)) ==
	    std::vector{fixture.upper});
	CHECK(
	    fixture.physics.Overlap(kRoom, "crate", {{0.0, -0.5, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}}) ==
	    std::vector{fixture.floor, fixture.lower});
	CHECK(fixture.physics.Overlap(kRoom, "crate", {{30.0, 10.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}})
	          .empty());
	CHECK_THROWS_AS((void)fixture.physics.Overlap(kRoom, "nothing", across), std::invalid_argument);
	CHECK_THROWS_AS(
	    (void)fixture.physics.Overlap(
	        kRoom, "crate", {{std::nan(""), 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}}),
	    std::invalid_argument);

	// A sphere needs no shape defined, and around nothing finds nothing.
	CHECK(
	    fixture.physics.OverlapSphere(kRoom, {0.0, 2.0, 0.0}, 0.5) ==
	    std::vector{fixture.lower, fixture.upper});
	CHECK(fixture.physics.OverlapSphere(kRoom, {0.0, 10.0, 0.0}, 1.0).empty());
	CHECK_THROWS_AS((void)fixture.physics.OverlapSphere(kRoom, {}, 0.0, {}), std::invalid_argument);
	CHECK_THROWS_AS(
	    (void)fixture.physics.OverlapSphere(kRoom, {}, std::numeric_limits<double>::infinity()),
	    std::invalid_argument);
	CHECK_THROWS_AS(
	    (void)fixture.physics.OverlapSphere(kRoom, {0.0, std::nan(""), 0.0}, 1.0),
	    std::invalid_argument);

	// The same again, in a world made the same way, is the same answers.
	Fixture again;
	again.Stack();
	again.Run(4.0);
	CHECK(entities(again.physics.CastRayAll(kRoom, above, down, 50.0)) == stack);
	CHECK(again.physics.CastRayAll(kRoom, above, down, 50.0).back().distance == ray.back().distance);
	CHECK(again.physics.Overlap(kRoom, "crate", across) == std::vector{again.lower, again.upper});
}


TEST_CASE("Physics: a pick meets a hull off its body's centre where the world's cast does")
{
	Fixture fixture;
	fixture.world.ShowInSnapshots<Body>();
	Physics::ShapeDefinition hull;
	hull.kind = Physics::ShapeKind::ConvexHull;
	for (const float x : {9.0f, 11.0f})
		for (const float y : {-1.0f, 1.0f})
			for (const float z : {-1.0f, 1.0f})
				hull.points.emplace_back(x, y, z);
	fixture.physics.DefineShape("off", hull);
	fixture.Add(BodyKind::Static, "off", {0.0, 5.0, 0.0});
	fixture.world.Step();
	const Sim::Snapshot snapshot = Sim::Capture(fixture.world);
	const Physics::SnapshotPicker picker{fixture.physics};
	const glm::dvec3 above{10.0, 20.0, 0.0};
	const std::optional<Physics::CastHit> cast =
	    fixture.physics.CastRay(kRoom, above, {0.0, -1.0, 0.0}, 50.0);
	const std::optional<Physics::CastHit> picked =
	    picker.CastRay(snapshot, snapshot, 1.0, kRoom, above, {0.0, -1.0, 0.0}, 50.0);
	REQUIRE(cast);
	REQUIRE(picked);
	CHECK(picked->distance == doctest::Approx(cast->distance).epsilon(1e-6));
}


TEST_CASE("Physics: casts tied in two local frames give the lower entity, as their all-hit lists do")
{
	// A crate in the room and one on the lift, their tops level: whichever frame is searched first.
	Fixture fixture;
	const entt::entity room = fixture.Add(BodyKind::Static, "crate", {0.0, 0.0, 0.0});
	const entt::entity lift = fixture.Add(BodyKind::Kinematic, "crate", {0.0, 0.0, 0.0});
	fixture.world.Registry().emplace<Physics::LocalFrame>(lift, "lift");
	fixture.world.Step();
	const glm::dvec3 above{0.5, 20.0, 0.0};
	const std::optional<Physics::CastHit> first =
	    fixture.physics.CastRay(kRoom, above, {0.0, -1.0, 0.0}, 50.0);
	const std::vector<Physics::CastHit> all =
	    fixture.physics.CastRayAll(kRoom, above, {0.0, -1.0, 0.0}, 50.0);
	REQUIRE(first);
	REQUIRE(all.size() >= 2);
	CHECK(all[0].distance == doctest::Approx(all[1].distance));
	CHECK(first->entity == all[0].entity);
	CHECK(first->entity == std::min(room, lift));
}


TEST_CASE("Physics: a sphere too small for a float to hold is refused, not handed to Jolt")
{
	Fixture fixture;
	CHECK_THROWS_AS(
	    (void)fixture.physics.OverlapSphere(kRoom, {0.0, 0.0, 0.0}, 1.0e-100), std::invalid_argument);
	CHECK_THROWS_AS(
	    (void)fixture.physics.OverlapSphere(kRoom, {0.0, 0.0, 0.0}, 1.0e100), std::invalid_argument);
}
