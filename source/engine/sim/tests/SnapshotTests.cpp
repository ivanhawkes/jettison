#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/sim/Stepper.h>

#include <jettison/reflect/Reflect.h>

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <numbers>
#include <vector>

using namespace Jettison;

namespace
{
// Moves along x at a metre a second and turns about y at a radian a second, so a state between
// two steps has a position and an orientation to check.
struct Drift
{
	double speed{1.0};
};

struct Launch
{
	double x{0.0};
};
struct Retire
{
	entt::entity entity{entt::null};
};

// Events: plain structs, which need no registration.
struct Launched
{
	entt::entity entity{entt::null};
};
struct Passed
{
	entt::entity entity{entt::null};
	double x{0.0};
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using Reflect::Attribute;
		Sim::RegisterTypes();
		Reflect::RegisterType<Drift>("SnapshotTests.Drift")
		    .Field<&Drift::speed>("speed", Attribute::Saved);
		Reflect::RegisterType<Launch>("SnapshotTests.Launch").Field<&Launch::x>("x", Attribute::Saved);
		Reflect::RegisterType<Retire>("SnapshotTests.Retire")
		    .Field<&Retire::entity>("entity", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

constexpr double kStep = 0.125;

glm::dquat Turn(double radians)
{
	return glm::angleAxis(radians, glm::dvec3{0.0, 1.0, 0.0});
}

void Populate(Sim::SimWorld &world)
{
	world.RegisterComponent<Sim::Transform>();
	world.RegisterComponent<Drift>();
	world.RegisterCommand<Launch>(
	    [](Sim::SimWorld &target, const Launch &command)
	    {
		    entt::registry &registry = target.Registry();
		    const entt::entity entity = registry.create();
		    registry.emplace<Sim::Transform>(entity, glm::dvec3{command.x, 0.0, 0.0}, Turn(0.0));
		    registry.emplace<Drift>(entity);
		    target.Events().Emit(Launched{entity});
	    });
	world.RegisterCommand<Retire>([](Sim::SimWorld &target, const Retire &command)
	                              { target.Registry().destroy(command.entity); });
	world.RegisterSystem(
	    {"drift",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Drift>().Writing<Sim::Transform>(),
	     {},
	     [](entt::registry &registry, double delta)
	     {
		     auto &events = registry.ctx().get<Sim::EventQueue>();
		     for (auto [entity, transform, drift] : registry.view<Sim::Transform, const Drift>().each())
		     {
			     const double before = transform.position.x;
			     transform.position.x += drift.speed * delta;
			     transform.orientation = Turn(drift.speed * delta) * transform.orientation;
			     if (before < 1.0 && transform.position.x >= 1.0)
				     events.Emit(Passed{entity, transform.position.x});
		     }
	     }});
}
} // namespace

TEST_CASE("Sim: a snapshot holds every transform, sorted, at the step it was taken")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, 1};
	Populate(world);
	world.Commands().Push(Launch{0.0});
	world.Commands().Push(Launch{10.0});
	world.Commands().Push(Launch{20.0});
	world.Step();
	// An entity without a transform is not presentation's to see.
	const entt::entity hidden = world.Registry().create();
	world.Registry().destroy(entt::entity{1});

	const Sim::Snapshot snapshot = Sim::Capture(world);
	CHECK(snapshot.step == 1);
	CHECK(snapshot.time == kStep);
	REQUIRE(snapshot.transforms.size() == 2);
	CHECK(snapshot.transforms[0].entity == entt::entity{0});
	CHECK(snapshot.transforms[1].entity == entt::entity{2});
	REQUIRE(snapshot.Find(entt::entity{2}) != nullptr);
	CHECK(snapshot.Find(entt::entity{2})->position.x == 20.0 + kStep);
	CHECK(snapshot.Find(entt::entity{1}) == nullptr);
	CHECK(snapshot.Find(hidden) == nullptr);
	CHECK(snapshot.Find(entt::null) == nullptr);
}

TEST_CASE("Sim: interpolation runs along the line and the shorter arc, and knows births and deaths")
{
	const auto entity = entt::entity{3};
	const auto born = entt::entity{4};
	const auto died = entt::entity{5};
	Sim::Snapshot previous{
	    1, kStep, {{entity, {{0.0, 0.0, 0.0}, Turn(0.0)}}, {died, {{9.0, 0.0, 0.0}, Turn(0.0)}}}};
	Sim::Snapshot current{
	    2,
	    2 * kStep,
	    {{entity, {{2.0, 4.0, -8.0}, Turn(std::numbers::pi / 2)}},
	     {born, {{7.0, 0.0, 0.0}, Turn(1.0)}}}};

	const auto halfway = Sim::Interpolate(previous, current, entity, 0.5);
	REQUIRE(halfway);
	CHECK(halfway->position == glm::dvec3{1.0, 2.0, -4.0});
	CHECK(glm::angle(halfway->orientation) == doctest::Approx(std::numbers::pi / 4));
	CHECK(Sim::Interpolate(previous, current, entity, 0.0)->position == glm::dvec3{0.0});

	// A quarter turn the long way round is three quarters; the shorter arc goes the other way.
	current.transforms[0].transform.orientation = -Turn(std::numbers::pi / 2);
	CHECK(
	    glm::angle(Sim::Interpolate(previous, current, entity, 0.5)->orientation) ==
	    doctest::Approx(std::numbers::pi / 4));

	const auto newborn = Sim::Interpolate(previous, current, born, 0.25);
	REQUIRE(newborn);
	CHECK(newborn->position.x == 7.0);
	CHECK(!Sim::Interpolate(previous, current, died, 0.25));
	CHECK(!Sim::Interpolate(previous, current, entt::entity{6}, 0.25));
}

TEST_CASE("Sim: the stepper runs the frame's steps and keeps the states either side of the last")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, 1};
	Populate(world);
	Sim::Stepper stepper{world};
	CHECK(stepper.Previous().step == 0);
	CHECK(stepper.Current().step == 0);

	world.Commands().Push(Launch{0.0});
	// Two and a half steps: two run, and presentation is half way from the first to the second.
	CHECK(stepper.Advance(2.5 * kStep) == 2);
	CHECK(world.Clock().Step() == 2);
	CHECK(stepper.Previous().step == 1);
	CHECK(stepper.Current().step == 2);
	CHECK(stepper.Alpha() == doctest::Approx(0.5));
	const entt::entity entity{0};
	const auto shown = Sim::Interpolate(stepper.Previous(), stepper.Current(), entity, stepper.Alpha());
	REQUIRE(shown);
	CHECK(shown->position.x == doctest::Approx(1.5 * kStep));

	// A frame that finishes no step leaves the states, and moves presentation along between them.
	CHECK(stepper.Advance(0.25 * kStep) == 0);
	CHECK(stepper.Current().step == 2);
	CHECK(stepper.Alpha() == doctest::Approx(0.75));

	// One step: the state the last frame ended on is the one this frame interpolates from.
	CHECK(stepper.Advance(0.25 * kStep) == 1);
	CHECK(stepper.Previous().step == 2);
	CHECK(stepper.Current().step == 3);
	CHECK(stepper.Previous().Find(entity)->position.x == doctest::Approx(2 * kStep));

	// Paused, real time runs no steps; compressed, it runs more of them, not longer ones.
	world.SetPaused(true);
	CHECK(stepper.Advance(10 * kStep) == 0);
	world.SetPaused(false);
	world.SetCompression(4.0);
	CHECK(stepper.Advance(kStep) == 4);
	CHECK(stepper.Previous().step == 6);
	CHECK(stepper.Current().step == 7);
	// The accumulator's cap holds under compression too.
	world.SetCompression(100.0);
	CHECK(stepper.Advance(kStep) == stepper.Accumulator().MaxStepsPerFrame());
	CHECK(stepper.Accumulator().DroppedTime() > 0.0);
}

TEST_CASE("Sim: after a load, the stepper resyncs and does not interpolate across it")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, 1};
	Populate(world);
	Sim::Stepper stepper{world};
	world.Commands().Push(Launch{0.0});
	stepper.Advance(kStep);
	const std::vector<std::byte> save = world.Save();
	stepper.Advance(4 * kStep);

	world.Load(save);
	stepper.Resync();
	CHECK(stepper.Previous().step == 1);
	CHECK(stepper.Current().step == 1);
	CHECK(
	    Sim::Interpolate(stepper.Previous(), stepper.Current(), entt::entity{0}, 0.5)->position.x ==
	    kStep);
}

TEST_CASE("Sim: a transform is saved with the world, its orientation included")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, 1};
	Populate(world);
	world.Commands().Push(Launch{-3.0});
	world.Step();
	world.Step();
	const Sim::Transform saved = world.Registry().get<Sim::Transform>(entt::entity{0});

	Sim::SimWorld loaded{kStep, 2};
	Populate(loaded);
	loaded.Load(world.Save());
	const Sim::Transform &read = loaded.Registry().get<Sim::Transform>(entt::entity{0});
	CHECK(read.position == saved.position);
	CHECK(read.orientation == saved.orientation);
	CHECK(loaded.StateHash() == world.StateHash());
}

TEST_CASE("Sim: events reach presentation in the order they were raised, stamped with their step")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, 1};
	Populate(world);
	world.Commands().Push(Launch{1.0 - 1.5 * kStep});
	world.Step();
	world.Step();
	world.Commands().Push(Launch{0.0});
	world.Step();

	std::vector<Sim::SimEvent> events = world.Events().Take();
	REQUIRE(events.size() == 3);
	CHECK(events[0].step == 0);
	REQUIRE(events[0].event.try_cast<Launched>() != nullptr);
	CHECK(events[0].event.try_cast<Launched>()->entity == entt::entity{0});
	CHECK(events[1].step == 1);
	REQUIRE(events[1].event.try_cast<Passed>() != nullptr);
	CHECK(events[1].event.try_cast<Passed>()->x == doctest::Approx(1.0 + 0.5 * kStep));
	CHECK(events[2].step == 2);
	CHECK(events[2].event.try_cast<Launched>()->entity == entt::entity{1});
	CHECK(world.Events().Pending().empty());

	// Events are not state: pending ones leave the save and the hash alone, and a load drops them.
	world.Commands().Push(Launch{5.0});
	world.Step();
	REQUIRE(world.Events().Pending().size() == 1);
	Sim::SimWorld twin{kStep, 1};
	Populate(twin);
	twin.Load(world.Save());
	CHECK(twin.Events().Pending().empty());
	CHECK(twin.StateHash() == world.StateHash());
	world.Load(twin.Save());
	CHECK(world.Events().Pending().empty());
}
