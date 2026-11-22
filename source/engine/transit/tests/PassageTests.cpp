#include <jettison/transit/Passage.h>

#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/transit/Path.h>
#include <jettison/transit/Profile.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Passages as a game of no star system has them: a lift's car up its shaft on a profile, and a train
// along its line on a ferry that moves, the ferry placed by the game's frame model.
using namespace Jettison;

namespace
{
constexpr double kStep = 1.0 / 60.0;
const std::uint64_t kBuilding = Sim::FrameId("Building");
const std::uint64_t kHarbour = Sim::FrameId("Harbour");
// A lift's: 1.2 m/s² for two seconds at each end, at its speed between.
constexpr Transit::MotionProfile kLift{1.2f, 4.0f, 0.0f};
// The ferry's speed along x, from the harbour's origin at the start.
constexpr double kFerrySpeed = 3.0;

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
		// The game's carriers are its frame model's bodies.
		Transit::Passages::Attach(
		    world, physics, [this](std::uint64_t frame, std::string_view carrier, double time)
		    { return physics.FrameOrigin(frame, carrier, time); });
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


	entt::entity Car(std::uint64_t frame, const glm::dvec3 &at, Physics::BodyKind kind)
	{
		entt::registry &registry = world.Registry();
		const entt::entity car = registry.create();
		registry.emplace<Sim::Transform>(car, Sim::Transform{at, glm::dquat{1.0, 0.0, 0.0, 0.0}});
		registry.emplace<Sim::InFrame>(car, frame);
		Physics::Body body;
		body.kind = kind;
		body.shape = "car";
		body.mass = 800.0f;
		registry.emplace<Physics::Body>(car, body);
		registry.emplace<Physics::Velocity>(car);
		return car;
	}


	static Transit::Passage Way(
	    std::vector<glm::dvec3> path, std::optional<Transit::MotionProfile> profile = {},
	    std::string frame = {})
	{
		Transit::Passage passage;
		passage.frame = frame;
		passage.carrier = std::move(frame);
		passage.path = std::move(path);
		passage.profile = profile;
		return passage;
	}


	std::vector<Transit::PassageEnded> Ended()
	{
		std::vector<Transit::PassageEnded> found;
		for (const Sim::SimEvent &event : world.Events().Take())
			if (const auto *value = event.event.try_cast<Transit::PassageEnded>())
				found.push_back(*value);
		return found;
	}
};
} // namespace


TEST_CASE("Passage: a lift's car rises on its profile out of physics and comes out clear at the top")
{
	REQUIRE_NOTHROW(Transit::CheckProfile("The lift", kLift));
	Fixture fixture;
	entt::registry &registry = fixture.world.Registry();
	const entt::entity car = fixture.Car(kBuilding, {0.0, 0.0, 0.0}, Physics::BodyKind::Dynamic);
	// Another car already at the top, where the first would come out.
	const entt::entity parked = fixture.Car(kBuilding, {0.0, 40.0, 0.0}, Physics::BodyKind::Static);
	fixture.world.Step();

	const std::vector<glm::dvec3> shaft{{0.0, 0.0, 0.0}, {0.0, 40.0, 0.0}};
	const double seconds = Transit::Seconds(kLift, 40.0);
	Transit::Passages::Begin(fixture.world, car, Fixture::Way(shaft, kLift), seconds);
	CHECK_THROWS_AS(
	    Transit::Passages::Begin(fixture.world, car, Fixture::Way(shaft), seconds),
	    std::invalid_argument);
	CHECK_FALSE(registry.any_of<Physics::Body, Physics::Velocity, Physics::LocalFrame>(car));
	const Transit::Passage passage = registry.get<Transit::Passage>(car);
	CHECK(passage.body.shape == "car");
	CHECK(passage.arrivalStep - passage.departureStep == Transit::Steps(seconds, kStep));

	// Up the shaft, on the profile slowed to the whole steps, and nowhere else.
	double height = 0.0;
	while (registry.all_of<Transit::Passage>(car))
	{
		fixture.world.Step();
		if (!registry.all_of<Transit::Passage>(car))
			break;
		const Sim::Transform &at = registry.get<Sim::Transform>(car);
		const double share = static_cast<double>(fixture.world.Clock().Step() - passage.departureStep) /
		                     static_cast<double>(passage.arrivalStep - passage.departureStep);
		CHECK(at.position.y == doctest::Approx(Transit::Covered(kLift, 40.0, share * seconds)));
		CHECK(at.position.y > height);
		CHECK(at.position.x == 0.0);
		height = at.position.y;
	}
	CHECK(fixture.world.Clock().Step() == passage.arrivalStep);

	// Out at rest in the building's own frame, with its body back, beside the parked car.
	const std::vector<Transit::PassageEnded> ended = fixture.Ended();
	REQUIRE(ended.size() == 1);
	CHECK(ended[0].entity == car);
	CHECK(registry.get<Physics::Body>(car) == passage.body);
	CHECK(registry.get<Physics::LocalFrame>(car).body.empty());
	const glm::dvec3 out = registry.get<Sim::Transform>(car).position;
	const double apart =
	    2.0 * fixture.physics.ShapeRadius("car") + Transit::Passages::kArrivalClearance;
	CHECK(glm::distance(out, registry.get<Sim::Transform>(parked).position) >= apart - 1.0e-9);
	CHECK(out.y == doctest::Approx(40.0));
	fixture.world.Step();
	CHECK(glm::length(registry.get<Physics::Velocity>(car).linear) < 0.5);
}


TEST_CASE("Passage: a train on a moving ferry, fast-forwarded and loaded, arrives as one stepped does")
{
	// Along the deck and round its end, metres from the ferry's origin.
	const std::vector<glm::dvec3> line{{0.0, 1.0, 0.0}, {100.0, 1.0, 0.0}, {100.0, 1.0, 80.0}};
	Fixture forwarded;
	Fixture stepped;
	entt::entity train = entt::null;
	for (Fixture *fixture : {&forwarded, &stepped})
	{
		train = fixture->Car(kHarbour, line.front(), Physics::BodyKind::Dynamic);
		fixture->world.Registry().emplace<Physics::LocalFrame>(train, "ferry");
		fixture->world.Step();
		Transit::Passages::Begin(fixture->world, train, Fixture::Way(line, {}, "ferry"), 30.0);
	}
	const std::uint64_t arrival = forwarded.world.Registry().get<Transit::Passage>(train).arrivalStep;
	const std::uint64_t end = arrival + 60;
	int calls = 0;
	std::vector<std::byte> midway;
	while (forwarded.world.Clock().Step() < end)
	{
		forwarded.world.FastForward(end);
		++calls;
		while (stepped.world.Clock().Step() < forwarded.world.Clock().Step())
		{
			stepped.world.Step();
			if (stepped.world.Clock().Step() == arrival / 2)
				midway = stepped.world.Save();
		}
		REQUIRE(forwarded.world.Save() == stepped.world.Save());
	}
	// The passage's 1,800 steps in a handful of calls, and each of the 60 after it run.
	MESSAGE("The train's passage and a second on in " << calls << " calls");
	CHECK(calls < 70);

	// Out at the line's end, about where the ferry was as it arrived, in the ferry's frame.
	const entt::registry &registry = stepped.world.Registry();
	CHECK(registry.get<Physics::LocalFrame>(train).body == "ferry");
	CHECK_FALSE(registry.all_of<Transit::Passage>(train));

	Fixture loaded;
	REQUIRE_FALSE(midway.empty());
	loaded.world.Load(midway);
	REQUIRE(loaded.world.Registry().all_of<Transit::Passage>(train));
	const Sim::Transform &underway = loaded.world.Registry().get<Sim::Transform>(train);
	const double ferry = kFerrySpeed * loaded.world.Clock().Now();
	CHECK(underway.position.x > ferry);
	CHECK(underway.position.x < ferry + 100.0);
	CHECK(underway.position.z == 0.0);
	while (loaded.world.Clock().Step() < end)
		loaded.world.Step();
	CHECK(loaded.world.StateHash() == stepped.world.StateHash());
}


TEST_CASE("Passage: a way is any game's, and an arrival comes out on rings clear of what is there")
{
	const std::vector<glm::dvec3> way{
	    {0.0, 0.0, 0.0}, {3.0, 4.0, 0.0}, {3.0, 4.0, 0.0}, {3.0, 4.0, 12.0}};
	CHECK(Transit::Length(way) == 17.0);
	CHECK(Transit::Along(way, 0.0).first == glm::dvec3{0.0});
	CHECK(Transit::Along(way, 5.0 / 17.0).first.y == doctest::Approx(4.0));
	CHECK(Transit::Along(way, 1.0).first == glm::dvec3{3.0, 4.0, 12.0});
	CHECK(Transit::Along(way, 1.0).second == glm::dvec3{0.0, 0.0, 1.0});
	CHECK(Transit::Eased(0.0) == 0.0);
	CHECK(Transit::Eased(0.5) == 0.5);
	CHECK(Transit::Eased(1.0) == 1.0);
	CHECK(Transit::Steps(40.0, 1.0 / 60.0) == 2400);
	CHECK(Transit::Steps(0.0, 1.0 / 60.0) == 1);

	const Sim::Transform place{{0.0, 0.0, 0.0}, glm::dquat{1.0, 0.0, 0.0, 0.0}};
	const std::vector<Transit::Occupant> occupants{
	    {kBuilding, {0.0, 0.0, 0.0}, 1.0}, {kHarbour, {}, 50.0}};
	// The other frame's occupant is nowhere near; this one's moves it a ring out.
	const glm::dvec3 out = Transit::ClearPlace(occupants, kBuilding, place, 1.0, 0.5);
	CHECK(glm::length(out) == doctest::Approx(2.5));
	CHECK(out.z == 0.0);
	CHECK(Transit::ClearPlace(occupants, Sim::FrameId("Elsewhere"), place, 1.0, 0.5) == place.position);
}
