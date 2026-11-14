#include <jettison/physics/PhysicsWorld.h>

#include <jettison/celestial/Orbit.h>
#include <jettison/physics/Flight.h>
#include <jettison/sim/Snapshot.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "Halcyon.h"

using namespace Jettison;
using namespace Jettison::PhysicsTests;
using Physics::Body;
using Physics::BodyKind;
using Physics::CastOptions;
using Physics::ContactEvent;
using Physics::ContactPhase;
using Physics::ShapeDefinition;
using Physics::ShapeKind;
using Physics::Velocity;

namespace
{
constexpr double kStep = 1.0 / 60.0;
// A system no star system definition names: no gravity, no rails, and bodies that sleep.
const std::uint64_t kQuiet = Sim::SystemId("Quiet");

ShapeDefinition Box(glm::vec3 halfExtents)
{
	ShapeDefinition shape;
	shape.kind = ShapeKind::Box;
	shape.halfExtents = halfExtents;
	return shape;
}

Body Make(BodyKind kind, std::string shape, float mass = 0.0f)
{
	Body body;
	body.kind = kind;
	body.shape = std::move(shape);
	body.mass = mass;
	return body;
}

Body Sensor(BodyKind kind, std::string shape)
{
	Body body = Make(kind, std::move(shape));
	body.sensor = true;
	return body;
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
		world.RegisterComponent<Sim::InSystem>();
		Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
		physics.DefineStarSystem(Halcyon());
		physics.DefineShape("crate", Box({1.0f, 1.0f, 1.0f}));
		physics.DefineShape("block", Box({1.0f, 5.0f, 5.0f}));
		physics.DefineShape("zone", Box({5.0f, 5.0f, 5.0f}));
		physics.DefineShape("hub", MeshBox({6.0f, 6.0f, 30.0f}));
		return physics;
	}

	entt::entity Add(
	    const Body &body, const glm::dvec3 &position, const glm::vec3 &velocity = glm::vec3{0.0f},
	    std::uint64_t system = kQuiet)
	{
		entt::registry &registry = world.Registry();
		const entt::entity entity = registry.create();
		registry.emplace<Sim::Transform>(
		    entity, Sim::Transform{position, glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InSystem>(entity, system);
		registry.emplace<Body>(entity, body);
		if (body.kind != BodyKind::Static)
			registry.emplace<Velocity>(entity, velocity, glm::vec3{0.0f});
		return entity;
	}

	// The station on its rails in its own frame, with a docking zone off the hub's -Z end.
	entt::entity Station()
	{
		entt::registry &registry = world.Registry();
		const entt::entity station = registry.create();
		registry.emplace<Sim::Transform>(station);
		registry.emplace<Sim::InSystem>(station, kHalcyon);
		registry.emplace<Physics::OnRails>(station, std::string{kStation});
		registry.emplace<Physics::LocalFrame>(station, std::string{kStation});
		registry.emplace<Body>(station, Make(BodyKind::Kinematic, "hub"));
		return station;
	}
	entt::entity Zone(const glm::dvec3 &offset)
	{
		entt::registry &registry = world.Registry();
		const entt::entity zone = registry.create();
		registry.emplace<Sim::Transform>(zone);
		registry.emplace<Sim::InSystem>(zone, kHalcyon);
		registry.emplace<Physics::OnRails>(zone, std::string{kStation}, offset);
		registry.emplace<Physics::LocalFrame>(zone, std::string{kStation});
		registry.emplace<Body>(zone, Sensor(BodyKind::Kinematic, "zone"));
		return zone;
	}

	// Steps, gathering what each began and ended.
	void Run(int steps)
	{
		for (int step = 0; step < steps; ++step)
		{
			world.Step();
			log.insert(log.end(), physics.Contacts().begin(), physics.Contacts().end());
		}
	}
	bool Logged(ContactPhase phase, entt::entity first, entt::entity second) const
	{
		return std::ranges::any_of(
		    log, [&](const ContactEvent &event)
		    { return event.phase == phase && event.first == first && event.second == second; });
	}

	const glm::dvec3 &Position(entt::entity entity) const
	{
		return world.Registry().get<Sim::Transform>(entity).position;
	}
	glm::dvec3 StationAt(double simSeconds) const
	{
		return Celestial::EvaluateBody(Halcyon(), 2, simSeconds).metres;
	}

	Sim::SimWorld world{kStep, 1};
	Physics::PhysicsWorld &physics;
	std::vector<ContactEvent> log;
};
} // namespace

TEST_CASE(
    "Contacts: two bodies that meet begin a contact, with where and how hard, and end it as they part")
{
	Fixture fixture;
	// A crate at 10 m/s into a block whose face is at x = 4, which it bounces off.
	Body crate = Make(BodyKind::Dynamic, "crate", 100.0f);
	crate.restitution = 1.0f;
	const entt::entity moving = fixture.Add(crate, {0.0, 0.0, 0.0}, {10.0f, 0.0f, 0.0f});
	const entt::entity block = fixture.Add(Make(BodyKind::Static, "block"), {5.0, 0.0, 0.0});
	// 3 m to go: the eighteenth step meets it.
	fixture.Run(19);
	REQUIRE(fixture.log.size() == 1);
	const ContactEvent &began = fixture.log.front();
	CHECK(began.phase == ContactPhase::Began);
	CHECK(began.first == moving);
	CHECK(began.second == block);
	CHECK_FALSE(began.sensor);
	CHECK(began.speed == doctest::Approx(10.0f).epsilon(1e-3));
	CHECK(began.normal.x == doctest::Approx(1.0f).epsilon(1e-4));
	CHECK(began.point.x == doctest::Approx(4.0).epsilon(0.01));
	CHECK(fixture.physics.Touching(moving) == std::vector{block});
	CHECK(fixture.physics.Touching(block) == std::vector{moving});

	fixture.Run(30);
	CHECK(fixture.world.Registry().get<Velocity>(moving).linear.x < -9.0f);
	REQUIRE(fixture.log.size() == 2);
	CHECK(fixture.log.back() == ContactEvent{ContactPhase::Ended, moving, block});
	CHECK(fixture.physics.Touching(moving).empty());
	// A step with nothing beginning or ending has no contacts.
	CHECK(fixture.physics.Contacts().empty());
	CHECK(fixture.physics.Touching(entt::entity{99}).empty());
}

TEST_CASE("Contacts: a sensor sees a body enter and leave it, and pushes nothing")
{
	for (const BodyKind kind : {BodyKind::Static, BodyKind::Kinematic})
	{
		CAPTURE(static_cast<int>(kind));
		Fixture fixture;
		const entt::entity zone = fixture.Add(Sensor(kind, "zone"), {20.0, 0.0, 0.0});
		const entt::entity crate =
		    fixture.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {0.0, 0.0, 0.0}, {10.0f, 0.0f, 0.0f});
		// In at x = 14, by its leading face.
		fixture.Run(90);
		REQUIRE(fixture.log.size() == 1);
		CHECK(fixture.log.front().phase == ContactPhase::Began);
		CHECK(fixture.log.front().first == zone);
		CHECK(fixture.log.front().sensor);
		CHECK(fixture.physics.Touching(zone) == std::vector{crate});
		CHECK(fixture.world.Registry().get<Velocity>(crate).linear == glm::vec3{10.0f, 0.0f, 0.0f});
		// Out past x = 26, by its trailing face.
		fixture.Run(90);
		REQUIRE(fixture.log.size() == 2);
		CHECK(fixture.log.back() == ContactEvent{ContactPhase::Ended, zone, crate, true});
		CHECK(fixture.Position(zone) == glm::dvec3{20.0, 0.0, 0.0});
	}
}

TEST_CASE("Contacts: a body asleep still touches what it lies against, and a sensor still sees it")
{
	Fixture fixture;
	const entt::entity zone = fixture.Add(Sensor(BodyKind::Static, "zone"), {0.0, 0.0, 0.0});
	// At rest inside the zone, and 5 mm off the block's face, inside Jolt's speculative distance: it
	// touches both and falls asleep in half a second, with nothing to wake it.
	const entt::entity crate = fixture.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {2.995, 0.0, 0.0});
	const entt::entity block = fixture.Add(Make(BodyKind::Static, "block"), {5.0, 0.0, 0.0});
	fixture.Run(240);
	CHECK(fixture.physics.Touching(crate) == std::vector{zone, block});
	CHECK(fixture.log.size() == 2);

	// Pushed away, it wakes and parts from the block, and stays in the zone.
	fixture.world.Registry().get<Velocity>(crate).linear = {-1.0f, 0.0f, 0.0f};
	fixture.Run(30);
	CHECK(fixture.physics.Touching(crate) == std::vector{zone});
	CHECK(fixture.Logged(ContactPhase::Ended, crate, block));
}

TEST_CASE("Contacts: a body asleep is made again asleep, where it lay, from a save of another Jolt")
{
	Fixture fixture;
	const entt::entity zone = fixture.Add(Sensor(BodyKind::Static, "zone"), {0.0, 0.0, 0.0});
	const entt::entity crate = fixture.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {2.995, 0.0, 0.0});
	const entt::entity block = fixture.Add(Make(BodyKind::Static, "block"), {5.0, 0.0, 0.0});
	const entt::entity drifter =
	    fixture.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {-20.0, 0.0, 0.0}, {0.0f, 0.5f, 0.0f});
	fixture.Run(240);
	REQUIRE(fixture.physics.Asleep(crate));
	REQUIRE_FALSE(fixture.physics.Asleep(drifter));
	fixture.physics.SaveAsJoltVersion({5, 5, 0, Physics::BuiltJoltVersion().features});
	const std::vector<std::byte> save = fixture.world.Save();

	Fixture loaded;
	loaded.world.Load(save);
	CHECK(loaded.physics.Asleep(crate));
	CHECK_FALSE(loaded.physics.Asleep(drifter));
	CHECK(loaded.physics.Touching(crate) == std::vector{zone, block});
	// Nothing begins or ends, the crate sleeps on where it lay, and the drifter, with nothing to
	// warm-start, goes where it would have to the bit.
	const glm::dvec3 lay = loaded.Position(crate);
	fixture.log.clear();
	fixture.Run(120);
	loaded.Run(120);
	CHECK(loaded.log.empty());
	CHECK(fixture.log.empty());
	CHECK(loaded.physics.Asleep(crate));
	CHECK(loaded.Position(crate) == lay);
	CHECK(loaded.Position(drifter) == fixture.Position(drifter));
}

TEST_CASE(
    "Contacts: crates pressed into a block, made again from a save of another Jolt, settle "
    "millimetres from where they would have")
{
	// A row of four, each thrusting at 15 m/s² into the next and the first into the block, at rest
	// and asleep a step in thirty as the thrust wakes them; saved on such a step.
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity block = fixture.Add(Make(BodyKind::Static, "block"), {5.0, 0.0, 0.0});
	std::vector<entt::entity> crates;
	for (int index = 0; index < 4; ++index)
	{
		const entt::entity crate =
		    fixture.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {2.995 - 2.0 * index, 0.0, 0.0});
		registry.emplace<Physics::Thrusters>(crate);
		registry.emplace<Physics::FlightControls>(
		    crate, glm::vec3{1.0f, 0.0f, 0.0f}, glm::vec3{0.0f}, false);
		crates.push_back(crate);
	}
	fixture.Run(240);
	REQUIRE(fixture.physics.Asleep(crates.back()));
	fixture.physics.SaveAsJoltVersion({5, 5, 0, Physics::BuiltJoltVersion().features});
	const std::vector<std::byte> save = fixture.world.Save();

	Fixture loaded;
	loaded.world.Load(save);
	fixture.log.clear();
	// What the warm start is worth: without the impulses each contact starts its step from, the
	// row gives 1.2 mm in the first step and 7.1 mm by the tenth, and comes to rest 4.6 mm from
	// where the run not saved does (measured).
	const auto apart = [&]
	{
		double farthest = 0.0;
		for (const entt::entity crate : crates)
			farthest =
			    std::max(farthest, glm::distance(loaded.Position(crate), fixture.Position(crate)));
		return farthest;
	};
	double farthest = 0.0;
	for (int step = 0; step < 120; ++step)
	{
		fixture.Run(1);
		loaded.Run(1);
		farthest = std::max(farthest, apart());
	}
	CHECK(farthest > 1.0e-3);
	CHECK(farthest < 0.01);
	CHECK(apart() < 0.005);
	CHECK(loaded.log.empty());
	CHECK(loaded.physics.Touching(crates.front()) == std::vector{block, crates[1]});
}

TEST_CASE("Contacts: a body removed ends its contacts, and one made again begins its own afresh")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity zone = fixture.Add(Sensor(BodyKind::Static, "zone"), {0.0, 0.0, 0.0});
	const entt::entity crate = fixture.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {1.0, 0.0, 0.0});
	const entt::entity other = fixture.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {-1.5, 0.0, 0.0});
	fixture.Run(1);
	CHECK(fixture.physics.Touching(zone) == std::vector{crate, other});

	// A change of Body makes it again: an end and a beginning, in that order, in one step.
	registry.get<Body>(crate).friction = 0.5f;
	fixture.Run(1);
	REQUIRE(fixture.physics.Contacts().size() == 2);
	CHECK(fixture.physics.Contacts()[0] == ContactEvent{ContactPhase::Ended, zone, crate, true});
	CHECK(fixture.physics.Contacts()[1].phase == ContactPhase::Began);
	CHECK(fixture.physics.Contacts()[1].second == crate);

	// Gone, or out of the system, it touches nothing.
	registry.destroy(crate);
	registry.get<Sim::InSystem>(other).system = Sim::kNoSystem;
	fixture.Run(1);
	CHECK(
	    fixture.physics.Contacts() == std::vector{
	                                      ContactEvent{ContactPhase::Ended, zone, crate, true},
	                                      ContactEvent{ContactPhase::Ended, zone, other, true}});
	CHECK(fixture.physics.Touching(zone).empty());

	// A sensor is placed, not swept.
	Body swept = Sensor(BodyKind::Dynamic, "crate");
	swept.mass = 1.0f;
	swept.continuous = true;
	fixture.Add(swept, {0.0, 0.0, 0.0});
	CHECK_THROWS_WITH_AS(
	    fixture.world.Step(), doctest::Contains("a sensor is placed, not swept"),
	    std::invalid_argument);
}

TEST_CASE(
    "Contacts: what touches and what the last step began are saved, and a loaded world carries on")
{
	Fixture original;
	const entt::entity zone = original.Add(Sensor(BodyKind::Static, "zone"), {20.0, 0.0, 0.0});
	original.Add(Make(BodyKind::Dynamic, "crate", 100.0f), {0.0, 0.0, 0.0}, {10.0f, 0.0f, 0.0f});
	// Saved on the step the crate enters the zone, so the save holds the beginning.
	while (original.log.empty())
		original.Run(1);
	const std::vector<std::byte> save = original.world.Save();

	Fixture loaded;
	loaded.world.Load(save);
	CHECK(loaded.physics.Contacts() == original.physics.Contacts());
	CHECK(loaded.physics.Touching(zone) == original.physics.Touching(zone));
	original.Run(120);
	loaded.Run(120);
	CHECK(loaded.world.Save() == original.world.Save());
	CHECK(loaded.log == std::vector(original.log.begin() + 1, original.log.end()));
}

TEST_CASE(
    "Casts: a ray meets the first body along it in any frame of its system, and leaves out what it is "
    "told to")
{
	Fixture fixture;
	const entt::entity station = fixture.Station();
	const entt::entity zone = fixture.Zone({0.0, 0.0, -45.0});
	fixture.Run(1);
	const double now = fixture.world.Clock().Now();
	const glm::dvec3 at = fixture.StationAt(now);
	REQUIRE(fixture.Position(station) == at);
	REQUIRE(fixture.Position(zone) == at + glm::dvec3{0.0, 0.0, -45.0});

	// From 100 m off the hub's end towards it: the hull, 70 m on, facing back; the zone, which begins
	// 50 m from the station's centre, only when sensors are asked for.
	const glm::dvec3 eye = at + glm::dvec3{0.0, 1.0, -100.0};
	const glm::dvec3 ahead{0.0, 0.0, 2.0};
	const std::optional<Physics::CastHit> hull = fixture.physics.CastRay(kHalcyon, eye, ahead, 200.0);
	REQUIRE(hull);
	CHECK(hull->entity == station);
	CHECK(hull->distance == doctest::Approx(70.0).epsilon(1e-5));
	CHECK(glm::length(hull->point - (at + glm::dvec3{0.0, 1.0, -30.0})) < 1e-3);
	CHECK(hull->normal.z == doctest::Approx(-1.0f));
	const auto sensed = fixture.physics.CastRay(kHalcyon, eye, ahead, 200.0, {entt::null, true, {}});
	REQUIRE(sensed);
	CHECK(sensed->entity == zone);
	CHECK(sensed->distance == doctest::Approx(50.0).epsilon(1e-5));
	CHECK_FALSE(fixture.physics.CastRay(kHalcyon, eye, ahead, 200.0, {station, false, {}}));
	CHECK_FALSE(fixture.physics.CastRay(kHalcyon, eye, ahead, 60.0));
	CHECK_FALSE(fixture.physics.CastRay(kQuiet, eye, ahead, 200.0));

	// A crate in the system's own frame, 20 m before the hub as the next step leaves it, is nearer:
	// every frame is cast.
	const glm::dvec3 next = fixture.StationAt(now + kStep);
	const entt::entity crate = fixture.Add(
	    Make(BodyKind::Static, "crate"), next + glm::dvec3{0.0, 1.0, -50.0}, glm::vec3{0.0f}, kHalcyon);
	fixture.Run(1);
	const auto nearer =
	    fixture.physics.CastRay(kHalcyon, next + glm::dvec3{0.0, 1.0, -100.0}, ahead, 200.0);
	REQUIRE(nearer);
	CHECK(nearer->entity == crate);
	CHECK(nearer->distance == doctest::Approx(49.0).epsilon(1e-5));

	// Presentation shows the world half a step behind: a ray drawn then at the station misses it as
	// the last step left it, 250 m on along its orbit, and meets it with the frames placed at the
	// time it was drawn.
	const double shown = fixture.world.Clock().Now() - kStep / 2.0;
	const glm::dvec3 shownEye = fixture.StationAt(shown) + glm::dvec3{5.0, 1.0, -100.0};
	CHECK_FALSE(fixture.physics.CastRay(kHalcyon, shownEye, ahead, 200.0));
	const auto then =
	    fixture.physics.CastRay(kHalcyon, shownEye, ahead, 200.0, {entt::null, false, shown});
	REQUIRE(then);
	CHECK(then->entity == station);
	CHECK(then->distance == doctest::Approx(70.0).epsilon(1e-5));

	CHECK_THROWS_AS(
	    fixture.physics.CastRay(kHalcyon, eye, glm::dvec3{0.0}, 10.0), std::invalid_argument);
	CHECK_THROWS_AS(fixture.physics.CastRay(kHalcyon, eye, ahead, 0.0), std::invalid_argument);
	CHECK_THROWS_AS(
	    fixture.physics.CastRay(kHalcyon, glm::dvec3{std::nan("")}, ahead, 10.0),
	    std::invalid_argument);
}

TEST_CASE("Casts: a shape swept towards the station stops where it would meet the hub")
{
	Fixture fixture;
	const entt::entity station = fixture.Station();
	fixture.Run(1);
	const glm::dvec3 at = fixture.StationAt(fixture.world.Clock().Now());

	// A crate, 1 m either side, from 100 m off the hub's end: it meets it after 69 m.
	const Sim::Transform start{at + glm::dvec3{0.0, 0.0, -100.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}};
	const auto hit = fixture.physics.CastShape(kHalcyon, "crate", start, {0.0, 0.0, 1.0}, 200.0);
	REQUIRE(hit);
	CHECK(hit->entity == station);
	CHECK(hit->distance == doctest::Approx(69.0).epsilon(1e-4));
	CHECK(hit->point.z - at.z == doctest::Approx(-30.0).epsilon(1e-4));
	CHECK(hit->normal.z == doctest::Approx(-1.0f).epsilon(1e-4));
	// Past the hub's side, it meets nothing. The block, 5 m either side along z, meets it 4 m sooner,
	// and turned a quarter about y, so its 1 m leads, as soon as the crate.
	CHECK_FALSE(fixture.physics.CastShape(
	    kHalcyon, "crate", {start.position + glm::dvec3{8.0, 0.0, 0.0}, start.orientation},
	    {0.0, 0.0, 1.0}, 200.0));
	const auto lengthwise = fixture.physics.CastShape(kHalcyon, "block", start, {0.0, 0.0, 1.0}, 200.0);
	REQUIRE(lengthwise);
	CHECK(lengthwise->distance == doctest::Approx(65.0).epsilon(1e-4));
	const glm::dquat turned = glm::angleAxis(glm::half_pi<double>(), glm::dvec3{0.0, 1.0, 0.0});
	const auto across =
	    fixture.physics.CastShape(kHalcyon, "block", {start.position, turned}, {0.0, 0.0, 1.0}, 200.0);
	REQUIRE(across);
	CHECK(across->distance == doctest::Approx(69.0).epsilon(1e-4));

	CHECK_THROWS_WITH_AS(
	    fixture.physics.CastShape(kHalcyon, "hub", start, {0.0, 0.0, 1.0}, 10.0),
	    doctest::Contains("is a mesh"), std::invalid_argument);
	CHECK_THROWS_WITH_AS(
	    fixture.physics.CastShape(kHalcyon, "nothing", start, {0.0, 0.0, 1.0}, 10.0),
	    doctest::Contains("not defined"), std::invalid_argument);
}

TEST_CASE("Picking: a ray presentation draws is answered with what it hit, as an event")
{
	Fixture fixture;
	const entt::entity station = fixture.Station();
	fixture.Run(1);
	const double now = fixture.world.Clock().Now();
	const glm::dvec3 eye = fixture.StationAt(now) + glm::dvec3{0.0, 0.0, -100.0};
	// Each player's presentation asks, and takes its own answers.
	const Sim::Controller player{Sim::ControllerKind::Player, 1};
	fixture.world.Commands().Push(Physics::Pick{player, 1, kHalcyon, eye, {0.0, 0.0, 1.0}, 500.0, now});
	// One of nothing: a ray of no direction.
	fixture.world.Commands().Push(Physics::Pick{{}, 2, kHalcyon, eye, glm::dvec3{0.0}, 500.0, now});
	// And one that misses.
	fixture.world.Commands().Push(Physics::Pick{{}, 3, kHalcyon, eye, {0.0, 0.0, -1.0}, 500.0, now});
	fixture.world.Step();

	const std::vector<Sim::SimEvent> events = fixture.world.Events().Take();
	REQUIRE(events.size() == 3);
	const auto *hit = events[0].event.try_cast<Physics::Picked>();
	REQUIRE(hit);
	CHECK(hit->sender == player);
	CHECK(hit->request == 1);
	CHECK(hit->entity == station);
	CHECK(hit->distance == doctest::Approx(70.0).epsilon(1e-5));
	for (std::size_t index : {1u, 2u})
	{
		const auto *none = events[index].event.try_cast<Physics::Picked>();
		REQUIRE(none);
		CHECK(none->sender == Sim::Controller{});
		CHECK(none->request == index + 1);
		CHECK(none->entity == entt::null);
	}
}
