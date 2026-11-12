#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/SimWorld.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>

#include <doctest/doctest.h>

#include <glm/vec3.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using Reflect::Attribute;

namespace
{
// A small trading world: ships that move, mine ore at random, trade it and are destroyed, so a
// step touches every kind of state a save keeps.
struct Position
{
	glm::dvec3 value{0.0};
};
struct Velocity
{
	glm::dvec3 value{0.0};
};
struct Hold
{
	std::int64_t credits{0};
	std::uint32_t ore{0};
};
struct Ship
{
	std::string name;
	// Another entity, which the save keeps as its identifier.
	entt::entity escort{entt::null};
};

struct Spawn
{
	std::string name;
	glm::dvec3 position{0.0};
};
struct SetVelocity
{
	entt::entity ship{entt::null};
	glm::dvec3 velocity{0.0};
};
struct Sell
{
	entt::entity ship{entt::null};
	std::uint32_t ore{0};
	std::int64_t price{0};
};
struct Scrap
{
	entt::entity ship{entt::null};
};
struct Unhandled
{
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<Position>("SimTests.Position").Field<&Position::value>("value", Attribute::Saved);
		RegisterType<Velocity>("SimTests.Velocity").Field<&Velocity::value>("value", Attribute::Saved);
		RegisterType<Hold>("SimTests.Hold")
		    .Field<&Hold::credits>("credits", Attribute::Saved)
		    .Field<&Hold::ore>("ore", Attribute::Saved);
		RegisterType<Ship>("SimTests.Ship")
		    .Field<&Ship::name>("name", Attribute::Saved)
		    .Field<&Ship::escort>("escort", Attribute::Saved);
		RegisterType<Spawn>("SimTests.Spawn")
		    .Field<&Spawn::name>("name", Attribute::Saved)
		    .Field<&Spawn::position>("position", Attribute::Saved);
		RegisterType<SetVelocity>("SimTests.SetVelocity")
		    .Field<&SetVelocity::ship>("ship", Attribute::Saved)
		    .Field<&SetVelocity::velocity>("velocity", Attribute::Saved);
		RegisterType<Sell>("SimTests.Sell")
		    .Field<&Sell::ship>("ship", Attribute::Saved)
		    .Field<&Sell::ore>("ore", Attribute::Saved)
		    .Field<&Sell::price>("price", Attribute::Saved);
		RegisterType<Scrap>("SimTests.Scrap").Field<&Scrap::ship>("ship", Attribute::Saved);
		RegisterType<Unhandled>("SimTests.Unhandled");
		return true;
	}();
	(void)registered;
}

constexpr double kStep = 1.0 / 30.0;

// The game's start-up, the same every run.
void Populate(Sim::SimWorld &world, bool withHold = true)
{
	world.RegisterComponent<Position>();
	world.RegisterComponent<Velocity>();
	if (withHold)
		world.RegisterComponent<Hold>();
	world.RegisterComponent<Ship>();

	world.RegisterCommand<Spawn>(
	    [](Sim::SimWorld &target, const Spawn &command)
	    {
		    entt::registry &registry = target.Registry();
		    const entt::entity ship = registry.create();
		    registry.emplace<Ship>(ship, command.name, entt::null);
		    registry.emplace<Position>(ship, command.position);
		    registry.emplace<Velocity>(ship);
		    registry.emplace<Hold>(ship, 1000, 0u);
	    });
	world.RegisterCommand<SetVelocity>(
	    [](Sim::SimWorld &target, const SetVelocity &command)
	    {
		    // A command naming a ship that is gone is one the simulation rejects, not an error.
		    if (target.Registry().valid(command.ship))
			    target.Registry().replace<Velocity>(command.ship, command.velocity);
	    });
	world.RegisterCommand<Sell>(
	    [](Sim::SimWorld &target, const Sell &command)
	    {
		    if (!target.Registry().valid(command.ship))
			    return;
		    Hold &hold = target.Registry().get<Hold>(command.ship);
		    const std::uint32_t sold = std::min(hold.ore, command.ore);
		    hold.ore -= sold;
		    hold.credits += static_cast<std::int64_t>(sold) * command.price;
	    });
	world.RegisterCommand<Scrap>(
	    [](Sim::SimWorld &target, const Scrap &command)
	    {
		    if (target.Registry().valid(command.ship))
			    target.Registry().destroy(command.ship);
	    });

	world.RegisterSystem(
	    {"integrate",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Velocity>().Writing<Position>(),
	     {},
	     [](entt::registry &registry, double delta)
	     {
		     for (auto [entity, position, velocity] : registry.view<Position, const Velocity>().each())
			     position.value += velocity.value * delta;
	     }});
	// Draws from the world's generator once per ship, in the order the view visits them: that
	// order is part of the state a save must keep.
	world.RegisterSystem(
	    {"mine",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<>().Writing<Hold>(),
	     {"integrate"},
	     [](entt::registry &registry, double)
	     {
		     auto &random = registry.ctx().get<Math::Random>();
		     for (auto [entity, hold] : registry.view<Hold>().each())
			     hold.ore += static_cast<std::uint32_t>(random.Range(0, 3));
	     }});
	world.RegisterSystem(
	    {"escort",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<>().Writing<Ship>(),
	     {},
	     [](entt::registry &registry, double)
	     {
		     // Each ship escorts the previous one it visits; one that lost its
		     // escort to scrapping looks again.
		     entt::entity previous = entt::null;
		     for (auto [entity, ship] : registry.view<Ship>().each())
		     {
			     if (!registry.valid(ship.escort))
				     ship.escort = previous;
			     previous = entity;
		     }
	     }});
}

// The command stream: what a player would have done, as a function of the step. Entities are
// named by the identifiers the world hands out, which the stream can know because they are
// deterministic too: the first three ships are 0, 1 and 2, and the one spawned after 1 is
// scrapped reuses its index with the next version.
void Feed(Sim::SimWorld &world, std::uint64_t step)
{
	Sim::CommandQueue &commands = world.Commands();
	const auto ship = [](std::uint32_t identifier) { return entt::entity{identifier}; };
	switch (step)
	{
		case 0:
			commands.Push(Spawn{"Persistence", {0.0, 0.0, 0.0}});
			commands.Push(Spawn{"Fortitude", {1000.0, 0.0, 0.0}});
			commands.Push(Spawn{"Diligence", {0.0, 0.0, -5e8}});
			break;
		case 3:
			commands.Push(SetVelocity{ship(0), {10.0, 0.0, 0.5}});
			commands.Push(SetVelocity{ship(2), {0.0, -250.0, 1e3}});
			break;
		case 9:
			commands.Push(Sell{ship(1), 5, 1200});
			commands.Push(Scrap{ship(1)});
			break;
		case 12:
			commands.Push(Spawn{"Tenacity", {-3.0, 7.0, 11.0}});
			// Still names the scrapped ship: rejected.
			commands.Push(SetVelocity{ship(1), {1.0, 1.0, 1.0}});
			break;
		case 20:
			commands.Push(Sell{ship(0), 1000, 37});
			commands.Push(SetVelocity{ship(0x00100001u), {-4.0, 0.0, 0.0}});
			break;
		case 31: commands.Push(Scrap{ship(2)}); break;
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

constexpr std::uint64_t kSeed = 0x5eed;
constexpr std::uint64_t kSteps = 40;

// State kept outside the registry, as physics keeps Jolt's: a tally of the ore mined, which a
// system adds to every step.
class Tally final : public Sim::WorldService
{
  public:
	std::uint64_t ore{0};

	void Save(Serialise::BinaryWriter &writer) const override
	{
		writer.Write(ore);
	}
	std::function<void()> Load(Serialise::BinaryReader &reader, const entt::registry &loaded) override
	{
		std::uint64_t saved = 0;
		reader.Read(saved);
		// Checked against the registry it will have, not the one it has.
		std::uint64_t held = 0;
		for (auto [entity, hold] : loaded.view<const Hold>().each())
			held += hold.ore;
		if (saved < held)
			reader.Fail("the tally is less than the holds have");
		return [this, saved] { ore = saved; };
	}
};

Tally &AddTally(Sim::SimWorld &world, std::string name = "tally")
{
	auto owned = std::make_unique<Tally>();
	Tally &tally = *owned;
	world.AddService(name, std::move(owned));
	world.RegisterSystem(
	    {name,
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Hold>(),
	     {"mine"},
	     [&tally](entt::registry &registry, double)
	     {
		     for (auto [entity, hold] : registry.view<const Hold>().each())
			     tally.ore += hold.ore;
	     }});
	return tally;
}
} // namespace

TEST_CASE("Sim: commands drain in the order they were pushed, and the queue is then empty")
{
	Sim::CommandQueue queue;
	queue.Push(Spawn{"first", {}});
	queue.Push(Scrap{});
	CHECK(queue.Pending().size() == 2);
	const auto drained = queue.Drain();
	REQUIRE(drained.size() == 2);
	CHECK(drained[0].cast<const Spawn &>().name == "first");
	CHECK(drained[1].type() == entt::resolve<Scrap>());
	CHECK(queue.Pending().empty());
	CHECK_THROWS_AS(queue.Push(entt::meta_any{}), std::logic_error);
}

TEST_CASE("Sim: a step applies the commands, runs the systems in order, and advances the clock")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	CHECK(world.Clock().Step() == 0);

	world.Commands().Push(Spawn{"Persistence", {1.0, 2.0, 3.0}});
	world.Step();
	CHECK(world.Clock().Step() == 1);
	CHECK(world.Clock().Now() == kStep);
	const auto ship = entt::entity{0};
	REQUIRE(world.Registry().valid(ship));
	CHECK(world.Registry().get<Ship>(ship).name == "Persistence");
	CHECK(world.Registry().get<Hold>(ship).ore <= 3);

	world.Commands().Push(SetVelocity{ship, {30.0, 0.0, 0.0}});
	world.Step();
	// The command applied before integrate ran, so the ship moved by one step's worth.
	CHECK(world.Registry().get<Position>(ship).value.x == doctest::Approx(2.0));
}

TEST_CASE("Sim: the same seed and command stream give the same state, in every build")
{
	RegisterTypes();
	Sim::SimWorld first{kStep, kSeed};
	Sim::SimWorld second{kStep, kSeed};
	Populate(first);
	Populate(second);
	Run(first, kSteps);
	Run(second, kSteps);
	CHECK(first.Save() == second.Save());
	CHECK(first.StateHash() == second.StateHash());

	// Recorded from a Debug build and checked by the Release suite too: a change here is a change
	// to the simulation's results or to the save layout, and either is a decision, not drift. The
	// layout last changed in phase 7a (version 2, with the services' state).
	CHECK(first.StateHash() == 0x5a9262d95686c5c1ull);

	Sim::SimWorld reseeded{kStep, kSeed + 1};
	Populate(reseeded);
	Run(reseeded, kSteps);
	CHECK(reseeded.StateHash() != first.StateHash());
}

TEST_CASE("Sim: a world saved part-way and loaded carries on exactly as the original")
{
	RegisterTypes();
	Sim::SimWorld original{kStep, kSeed};
	Populate(original);
	Run(original, 15);
	// Commands still queued are part of the save.
	Feed(original, 15);
	original.Commands().Push(Sell{entt::entity{0}, 2, 99});
	const std::vector<std::byte> save = original.Save();

	// A different seed: everything the generator does after the load comes from the save.
	Sim::SimWorld loaded{kStep, 1};
	Populate(loaded);
	loaded.Load(save);
	CHECK(loaded.Save() == save);
	CHECK(loaded.Clock().Step() == 15);
	CHECK(loaded.Commands().Pending().size() == original.Commands().Pending().size());

	// Both step 15 with what is queued, then follow the stream; the loaded one recycles the same
	// entities and draws ore in the same order.
	original.Step();
	loaded.Step();
	Run(original, kSteps);
	Run(loaded, kSteps);
	CHECK(loaded.Save() == original.Save());
	CHECK(loaded.Registry().get<Ship>(entt::entity{0x00100001u}).name == "Tenacity");
}

// The world's step 15 as the phase 6d build saved it (commit a5d0c83, the trading world above), at
// version 1, before the world had services. A later version loads it through its migrations.
namespace
{
std::vector<std::byte> ReadFixture(const char *name)
{
	std::ifstream file{std::filesystem::path{JETTISON_SIM_TEST_DATA} / name, std::ios::binary};
	REQUIRE(file);
	const std::vector<char> text{
	    std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
	const auto *bytes = reinterpret_cast<const std::byte *>(text.data());
	return {bytes, bytes + text.size()};
}
} // namespace

TEST_CASE("Sim: a save written by version 1 loads through its migration and carries on")
{
	RegisterTypes();
	const std::vector<std::byte> fixture = ReadFixture("world-v1.save");
	CHECK(Serialise::PeekVersion(fixture, "Jettison.World") == 1);

	Sim::SimWorld loaded{kStep, 1};
	Populate(loaded);
	loaded.Load(fixture);
	CHECK(loaded.Clock().Step() == 15);

	// It is the state this build reaches at step 15, saved as this build saves it.
	Sim::SimWorld current{kStep, kSeed};
	Populate(current);
	Run(current, 15);
	CHECK(loaded.Save() == current.Save());

	// And it steps on to the pinned state of the whole run.
	Run(loaded, kSteps);
	CHECK(loaded.StateHash() == 0x5a9262d95686c5c1ull);

	// Version 1 had no services, so a world with one has no state to give it.
	Sim::SimWorld serviced{kStep, kSeed};
	Populate(serviced);
	AddTally(serviced);
	const std::vector<std::byte> before = serviced.Save();
	CHECK_THROWS_AS(serviced.Load(fixture), Serialise::FormatError);
	CHECK(serviced.Save() == before);

	// A version this build does not know, a later one's, is refused by name.
	std::vector<std::byte> later = current.Save();
	Serialise::BinaryDocument document = Serialise::BinaryDocument::Parse(later, "Jettison.World");
	document.version = 3;
	CHECK_THROWS_WITH_AS(
	    loaded.Load(document.Emit()),
	    "Jettison.World: version 3 found, version 2 expected: it was written by a later build",
	    Serialise::FormatVersionError);
}

TEST_CASE("Sim: the recorded commands replay a session from its start")
{
	RegisterTypes();
	Sim::SimWorld session{kStep, kSeed};
	Populate(session);
	session.SetRecording(true);
	Run(session, kSteps);
	const std::vector<Sim::AppliedCommand> &recorded = session.Recorded();
	REQUIRE(recorded.size() == 12);
	CHECK(recorded.front().step == 0);
	CHECK(recorded.back().step == 31);

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

TEST_CASE("Sim: a system's own command is applied at once and not recorded, so a replay sends it again")
{
	RegisterTypes();
	// A broker that sells a ship's ore as it reaches ten, by the same command a player sends.
	const auto broker = [](Sim::SimWorld &world)
	{
		world.RegisterSystem(
		    {"broker",
		     Ecs::TickPhase::Simulation,
		     Ecs::Access::Of<Hold>(),
		     {"mine"},
		     [&world](entt::registry &registry, double)
		     {
			     std::vector<entt::entity> full;
			     for (const auto [entity, hold] : registry.view<const Hold>().each())
				     if (hold.ore >= 10)
					     full.push_back(entity);
			     for (const entt::entity ship : full)
			     {
				     world.Apply(Sell{ship, 10, 7});
				     CHECK(registry.get<Hold>(ship).ore < 10);
			     }
		     }});
	};
	Sim::SimWorld session{kStep, kSeed};
	Populate(session);
	broker(session);
	session.SetRecording(true);
	Run(session, kSteps);
	// Only what was fed is recorded: the broker's sales are the state's own.
	CHECK(session.Recorded().size() == 12);
	std::int64_t credits = 0;
	for (const auto [entity, hold] : session.Registry().view<const Hold>().each())
		credits += hold.credits;
	CHECK(credits > 0);

	Sim::SimWorld replay{kStep, kSeed};
	Populate(replay);
	broker(replay);
	std::size_t next = 0;
	while (replay.Clock().Step() < kSteps)
	{
		for (;
		     next < session.Recorded().size() && session.Recorded()[next].step == replay.Clock().Step();
		     ++next)
			replay.Commands().Push(session.Recorded()[next].command);
		replay.Step();
	}
	CHECK(replay.StateHash() == session.StateHash());
	CHECK_THROWS_WITH_AS(
	    replay.Apply(Unhandled{}), doctest::Contains("no handler is registered for command"),
	    std::logic_error);
}

TEST_CASE("Sim: a save that does not fit is refused, and the world is left as it was")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	Run(world, 20);
	const std::vector<std::byte> save = world.Save();
	const std::uint64_t before = world.StateHash();

	for (const std::size_t size : {std::size_t{0}, std::size_t{10}, save.size() / 2, save.size() - 1})
	{
		CAPTURE(size);
		CHECK_THROWS_AS(world.Load(std::span{save}.first(size)), Serialise::FormatError);
		CHECK(world.StateHash() == before);
	}

	// A world that does not register Hold cannot hold what the save does.
	Sim::SimWorld smaller{kStep, kSeed};
	Populate(smaller, false);
	CHECK_THROWS_WITH_AS(
	    smaller.Load(save),
	    doctest::Contains("component 'SimTests.Hold', which this world does not register"),
	    Serialise::FormatError);
	CHECK(smaller.Clock().Step() == 0);
}

TEST_CASE("Sim: a service's state is saved with the world, and loaded with it or not at all")
{
	RegisterTypes();
	Sim::SimWorld original{kStep, kSeed};
	Populate(original);
	const Tally &tally = AddTally(original);
	CHECK_THROWS_WITH_AS(
	    AddTally(original), doctest::Contains("service 'tally' is already registered"),
	    std::logic_error);
	Run(original, 15);
	const std::vector<std::byte> save = original.Save();
	CHECK(tally.ore > 0);

	Sim::SimWorld loaded{kStep, 1};
	Populate(loaded);
	const Tally &loadedTally = AddTally(loaded);
	loaded.Load(save);
	CHECK(loadedTally.ore == tally.ore);
	CHECK(loaded.Save() == save);
	Run(original, kSteps);
	Run(loaded, kSteps);
	CHECK(loaded.StateHash() == original.StateHash());
	CHECK(loadedTally.ore == tally.ore);

	// A world without the service cannot hold the save, nor a world with one the save has not.
	Sim::SimWorld without{kStep, kSeed};
	Populate(without);
	CHECK_THROWS_WITH_AS(
	    without.Load(save), doctest::Contains("service 'tally', which this world does not have"),
	    Serialise::FormatError);
	Sim::SimWorld more{kStep, kSeed};
	Populate(more);
	AddTally(more);
	AddTally(more, "second");
	CHECK_THROWS_WITH_AS(
	    more.Load(save), doctest::Contains("no state for service 'second'"), Serialise::FormatError);

	// A service that refuses its state leaves the world as it was, its own state too.
	Sim::SimWorld refusing{kStep, kSeed};
	Populate(refusing);
	Tally &refusingTally = AddTally(refusing);
	refusingTally.ore = 0;
	Sim::SimWorld source{kStep, kSeed};
	Populate(source);
	Tally &sourceTally = AddTally(source);
	Run(source, 10);
	sourceTally.ore = 0;
	const std::uint64_t before = refusing.StateHash();
	CHECK_THROWS_WITH_AS(
	    refusing.Load(source.Save()), doctest::Contains("the tally is less than the holds have"),
	    Serialise::FormatError);
	CHECK(refusing.StateHash() == before);
}

TEST_CASE("Sim: registration mistakes and unknown commands are refused")
{
	RegisterTypes();
	CHECK_THROWS_AS((Sim::SimWorld{0.0, kSeed}), std::invalid_argument);
	Sim::SimWorld world{kStep, kSeed};
	Populate(world);
	struct NotReflected
	{
	};
	CHECK_THROWS_WITH_AS(
	    world.RegisterComponent<NotReflected>(), doctest::Contains("is not registered with Reflect"),
	    std::logic_error);
	CHECK_THROWS_WITH_AS(
	    world.RegisterComponent<Hold>(), doctest::Contains("already registered"), std::logic_error);
	CHECK_THROWS_AS(
	    world.RegisterCommand<Scrap>([](Sim::SimWorld &, const Scrap &) {}), std::logic_error);
	CHECK_THROWS_WITH_AS(
	    world.RegisterSystem({"draw", Ecs::TickPhase::Render, {}, {}, [](entt::registry &, double) {}}),
	    doctest::Contains("not a simulation-phase system"), std::logic_error);

	// Nothing queued with it is applied.
	world.Commands().Push(Spawn{"Persistence", {}});
	world.Commands().Push(Unhandled{});
	CHECK_THROWS_WITH_AS(
	    world.Step(), doctest::Contains("no handler is registered for command"), std::logic_error);
	CHECK(world.Registry().view<Ship>().empty());
	CHECK(world.Clock().Step() == 0);
}

TEST_CASE("Sim: a command's sender may command what it controls, and what no one does")
{
	entt::registry registry;
	const entt::entity free = registry.create();
	const entt::entity owned = registry.create();
	const Sim::Controller one{Sim::ControllerKind::Player, 1};
	const Sim::Controller two{Sim::ControllerKind::Player, 2};
	const Sim::Controller trader{Sim::ControllerKind::Ai, 1};
	registry.emplace<Sim::ControlledBy>(owned, one);
	CHECK(Sim::MayCommand(registry, free, two));
	CHECK(Sim::MayCommand(registry, owned, one));
	CHECK_FALSE(Sim::MayCommand(registry, owned, two));
	CHECK_FALSE(Sim::MayCommand(registry, owned, trader));
	// The game's own rules, which are no one's in particular.
	CHECK(Sim::MayCommand(registry, owned, {}));
}
