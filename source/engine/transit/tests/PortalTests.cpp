#include <jettison/transit/Portal.h>

#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/transit/Path.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Portals as a game of no star system has them: a door from a building's street onto a ferry that
// moves, and a lift's door from the lobby to the roof of the same building.
using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kBuilding = Sim::FrameId("Building");
const std::uint64_t kHarbour = Sim::FrameId("Harbour");

// The ferry's speed along x, from the harbour's origin at the start.
constexpr double kFerrySpeed = 3.0;

// Metres past a door's frame that an arrival comes out.
constexpr double kDoorDepth = 0.5;
const glm::dvec3 kRoof{0.0, 40.0, 0.0};
const glm::dvec3 kGangway{10.0, 1.0, 0.0};

class Harbour final : public Physics::FrameModel
{
  public:
	bool HasBody(std::string_view body) const override
	{
		return body == "ferry";
	}


	Sim::Transform BodyAt(std::string_view /*body*/, double simSeconds) const override
	{
		return {{kFerrySpeed * simSeconds, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}};
	}
};


struct Fixture
{
	Sim::SimWorld world{kStep, 7};
	Physics::PhysicsWorld &physics;


	Fixture() : physics{Setup(world)}
	{
		// The building's doors face along x, and the gangway, on the ferry, along z.
		Transit::Portals::Attach(
		    world, physics,
		    [this](std::uint64_t frame, std::string_view exit, double time) -> Transit::Exit
		    {
			    if (frame == kBuilding && exit == "roof")
				    return {{kRoof, Transit::Facing({1.0, 0.0, 0.0})}, "", kDoorDepth};
			    if (frame == kHarbour && exit == "gangway")
				    return {
				        {physics.FrameOrigin(frame, "ferry", time) + kGangway,
				         Transit::Facing({0.0, 0.0, 1.0})},
				        "ferry",
				        kDoorDepth};
			    throw std::invalid_argument{"no such exit"};
		    });
	}


	static Physics::PhysicsWorld &Setup(Sim::SimWorld &world)
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::Transform>();
		world.RegisterComponent<Sim::InFrame>();
		Physics::PhysicsWorld &physics = Physics::PhysicsWorld::Attach(world);
		Physics::ShapeDefinition car;
		car.halfExtents = {1.0f, 1.2f, 1.0f};
		physics.DefineShape("car", car);
		physics.DefineFrame(kHarbour, std::make_unique<Harbour>());
		return physics;
	}


	entt::entity Car(const glm::dvec3 &at, Physics::BodyKind kind)
	{
		entt::registry &registry = world.Registry();
		const entt::entity car = registry.create();
		registry.emplace<Sim::Transform>(car, Sim::Transform{at, glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InFrame>(car, kBuilding);
		Physics::Body body;
		body.kind = kind;
		body.shape = "car";
		body.mass = 800.0f;
		registry.emplace<Physics::Body>(car, body);
		registry.emplace<Physics::Velocity>(car);
		return car;
	}


	std::vector<Transit::PortalLeft> Left()
	{
		std::vector<Transit::PortalLeft> found;
		for (const Sim::SimEvent &event : world.Events().Take())
			if (const auto *value = event.event.try_cast<Transit::PortalLeft>())
				found.push_back(*value);
		return found;
	}
};
} // namespace


TEST_CASE("Portal: a door from a building's street comes out on a moving ferry, in its frame")
{
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity car = fixture.Car({0.0, 0.0, 0.0}, Physics::BodyKind::Dynamic);
	fixture.world.Step();

	Transit::Portals::Enter(fixture.world, car, "street", kHarbour, "gangway", 2.0);
	const Transit::InPortal portal = registry.get<Transit::InPortal>(car);
	CHECK(portal.from == kBuilding);
	CHECK(portal.entrance == "street");
	CHECK(portal.arrivalStep - fixture.world.Clock().Step() == Transit::Steps(2.0, kStep));
	CHECK(registry.get<Sim::InFrame>(car).frame == Sim::kNoFrame);
	CHECK(registry.all_of<Physics::Body>(car));

	// In no frame, it is in no doorway to go through again.
	CHECK_THROWS_AS(
	    Transit::Portals::Enter(fixture.world, car, "street", kHarbour, "gangway", 2.0),
	    std::invalid_argument);

	while (registry.all_of<Transit::InPortal>(car))
	{
		CHECK(registry.get<Sim::InFrame>(car).frame == Sim::kNoFrame);
		fixture.world.Step();
	}
	CHECK(fixture.world.Clock().Step() == portal.arrivalStep);
	const std::vector<Transit::PortalLeft> left = fixture.Left();
	REQUIRE(left.size() == 1);
	CHECK(left[0].entity == car);
	CHECK(left[0].frame == kHarbour);
	CHECK(left[0].exit == "gangway");

	// Out past the gangway's frame along z, where the ferry was as the step ended, on board.
	CHECK(registry.get<Sim::InFrame>(car).frame == kHarbour);
	CHECK(registry.get<Physics::LocalFrame>(car).body == "ferry");
	const glm::dvec3 gangway =
	    fixture.physics.FrameOrigin(kHarbour, "ferry", fixture.world.Clock().Now()) + kGangway;
	const double out =
	    kDoorDepth + fixture.physics.ShapeRadius("car") + Transit::Portals::kArrivalClearance;
	const glm::dvec3 at = registry.get<Sim::Transform>(car).position;
	CHECK(at.x == doctest::Approx(gangway.x));
	CHECK(at.y == doctest::Approx(gangway.y));
	CHECK(at.z == doctest::Approx(gangway.z + out));
}


TEST_CASE("Portal: two through a lift's door at once come out apart on the roof, forwarded as stepped")
{
	Fixture forwarded;
	Fixture stepped;
	entt::entity first = entt::null;
	entt::entity second = entt::null;
	entt::entity parked = entt::null;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		first = fixture->Car({0.0, 0.0, 0.0}, Physics::BodyKind::Dynamic);
		second = fixture->Car({0.0, 0.0, 10.0}, Physics::BodyKind::Dynamic);

		// Already where the first would come out.
		const double out =
		    kDoorDepth + fixture->physics.ShapeRadius("car") + Transit::Portals::kArrivalClearance;
		parked = fixture->Car(kRoof + glm::dvec3{out, 0.0, 0.0}, Physics::BodyKind::Static);
		fixture->world.Step();
		for (const entt::entity car : {first, second})
			Transit::Portals::Enter(fixture->world, car, "lobby", kBuilding, "roof", 3.0);
	}
	const std::uint64_t end = forwarded.world.Registry().get<Transit::InPortal>(first).arrivalStep + 30;
	int calls = 0;
	while (forwarded.world.Clock().Step() < end)
	{
		forwarded.world.FastForward(end);
		++calls;
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
			stepped.world.Step();
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}
	MESSAGE("Through the lift's door and half a second on in " << calls << " calls");
	CHECK(calls < 40);

	const entt::registry &registry = stepped.world.Registry();
	const std::vector<Transit::PortalLeft> left = stepped.Left();
	REQUIRE(left.size() == 2);
	CHECK(left[0].entity == first);
	CHECK(left[1].entity == second);
	const double apart =
	    2.0 * stepped.physics.ShapeRadius("car") + Transit::Portals::kArrivalClearance - 1.0e-9;
	const double out =
	    kDoorDepth + stepped.physics.ShapeRadius("car") + Transit::Portals::kArrivalClearance;
	std::vector<glm::dvec3> places;
	for (const entt::entity car : {first, second})
	{
		CHECK(registry.get<Sim::InFrame>(car).frame == kBuilding);
		CHECK(registry.get<Physics::LocalFrame>(car).body.empty());
		CHECK(glm::length(registry.get<Physics::Velocity>(car).linear) < 0.5);
		places.push_back(registry.get<Sim::Transform>(car).position);

		// On rings across the way the door faces, so as far along x as the door's out.
		CHECK(places.back().x == doctest::Approx(out).epsilon(0.01));
	}

	// Apart from each other and from the parked car, give or take what physics has done since.
	CHECK(glm::distance(places[0], places[1]) >= apart - 0.1);
	CHECK(glm::distance(places[0], registry.get<Sim::Transform>(parked).position) >= apart - 0.1);
}


TEST_CASE("Portal: portals need the game's exits, and one it does not have changes nothing")
{
	Sim::SimWorld world{kStep, 7};
	Physics::PhysicsWorld &physics = Fixture::Setup(world);
	CHECK_THROWS_AS(Transit::Portals::Attach(world, physics, {}), std::invalid_argument);

	Fixture fixture;
	const entt::entity car = fixture.Car({0.0, 0.0, 0.0}, Physics::BodyKind::Dynamic);
	fixture.world.Step();
	Transit::Portals::Enter(fixture.world, car, "lobby", kBuilding, "cellar", 0.0);
	CHECK_THROWS_AS(fixture.world.Step(), std::invalid_argument);
	CHECK(fixture.world.Registry().all_of<Transit::InPortal>(car));
	CHECK(fixture.world.Registry().get<Sim::InFrame>(car).frame == Sim::kNoFrame);
}
