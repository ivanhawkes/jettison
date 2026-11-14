#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/SimWorld.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>

#include <doctest/doctest.h>

#include <glm/vec3.hpp>

#include <bit>
#include <cmath>
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
	// layout last changed in phase 12a (version 3, with each component storage's version).
	CHECK(first.StateHash() == 0x6cbbb633b2549661ull);

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
	CHECK(loaded.StateHash() == 0x6cbbb633b2549661ull);

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
	document.version = 4;
	CHECK_THROWS_WITH_AS(
	    loaded.Load(document.Emit()),
	    "Jettison.World: version 4 found, version 3 expected: it was written by a later build",
	    Serialise::FormatVersionError);
}

// The same world's step 15, with its tally, as the phase 11 build saved it (commit cfb86e9), at
// version 2, before each component storage had a version.
TEST_CASE("Sim: a save written by version 2 loads through its migration, every storage at version 1")
{
	RegisterTypes();
	const std::vector<std::byte> fixture = ReadFixture("world-v2.save");
	CHECK(Serialise::PeekVersion(fixture, "Jettison.World") == 2);

	Sim::SimWorld loaded{kStep, 1};
	Populate(loaded);
	const Tally &tally = AddTally(loaded);
	loaded.Load(fixture);
	CHECK(loaded.Clock().Step() == 15);
	CHECK(tally.ore > 0);

	Sim::SimWorld current{kStep, kSeed};
	Populate(current);
	AddTally(current);
	Run(current, 15);
	CHECK(loaded.Save() == current.Save());
	Run(loaded, kSteps);
	Run(current, kSteps);
	CHECK(loaded.StateHash() == current.StateHash());

	// A component at version 2 cannot read a save that has it at version 1 without a step.
	Sim::SimWorld stepless{kStep, kSeed};
	stepless.RegisterComponent<Position>(Sim::ComponentMigrations{"SimTests.Position", {2}});
	stepless.RegisterComponent<Velocity>();
	stepless.RegisterComponent<Hold>();
	stepless.RegisterComponent<Ship>();
	AddTally(stepless);
	CHECK_THROWS_WITH_AS(
	    stepless.Load(fixture),
	    "SimTests.Position: version 1 found, version 2 expected: there is no migration from version 1",
	    Serialise::FormatVersionError);
}

namespace
{
// A component as an earlier build had it, registered under its own name and renamed in the save to
// the component's (BinaryDocument::Rename), as a build that had it under that name wrote it: a grade
// the component has since dropped, and its value a float of credits it has since made whole.
struct CargoV1
{
	std::uint32_t tonnes{0};
	std::string grade;
	float value{0.0f};
};
// Version 2: the grade gone, a fine grade's tonnes counted half again, and the value in whole
// credits, as the simulation keeps currency.
struct Cargo
{
	std::uint32_t tonnes{0};
	std::int64_t value{0};
};
// Version 3, a later build's, for the refusal.
struct CargoV3
{
	std::uint32_t tonnes{0};
};

void RegisterCargo()
{
	static const bool registered = []
	{
		Reflect::RegisterType<CargoV1>("SimTests.CargoV1")
		    .Field<&CargoV1::tonnes>("tonnes", Attribute::Saved)
		    .Field<&CargoV1::grade>("grade", Attribute::Saved)
		    .Field<&CargoV1::value>("value", Attribute::Saved);
		Reflect::RegisterType<Cargo>("SimTests.Cargo")
		    .Field<&Cargo::tonnes>("tonnes", Attribute::Saved)
		    .Field<&Cargo::value>("value", Attribute::Saved);
		Reflect::RegisterType<CargoV3>("SimTests.CargoV3")
		    .Field<&CargoV3::tonnes>("tonnes", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

Sim::ComponentMigrations CargoMigrations()
{
	Sim::ComponentMigrations chain{"SimTests.Cargo", {2}};
	chain.Add(
	    1,
	    [](Serialise::BinaryRecord &record)
	    {
		    if (record.Has("grade"))
		    {
			    if (record.Get<std::string>("grade") == "fine" && record.Has("tonnes"))
				    record.Set("tonnes", record.Get<std::uint32_t>("tonnes") * 3 / 2);
			    record.Remove("grade");
		    }
		    // A float's bits, read with the reader's primitives by the layout version 1 had.
		    if (record.Has("value"))
		    {
			    Serialise::BinaryReader reader = record.Open("value");
			    const auto bits = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
			    reader.End();
			    record.Set("value", static_cast<std::int64_t>(std::lround(std::bit_cast<float>(bits))));
		    }
	    });
	return chain;
}

std::vector<std::byte> SavedCargo()
{
	Sim::SimWorld old{kStep, kSeed};
	old.RegisterComponent<Position>();
	old.RegisterComponent<CargoV1>();
	entt::registry &registry = old.Registry();
	const entt::entity first = registry.create();
	registry.emplace<Position>(first, glm::dvec3{1.0, 2.0, 3.0});
	registry.emplace<CargoV1>(first, 40u, "fine", 1250.6f);
	const entt::entity second = registry.create();
	registry.emplace<CargoV1>(second, 12u, "coarse", 99.4f);
	// One saved before it had a value, which the step leaves out.
	const entt::entity third = registry.create();
	registry.emplace<CargoV1>(third, 7u, "", 0.0f);
	Serialise::BinaryDocument document = Serialise::BinaryDocument::Parse(old.Save(), "Jettison.World");
	document.Rename("SimTests.CargoV1", "SimTests.Cargo");
	return document.Emit();
}
} // namespace

TEST_CASE("Sim: a component's chain migrates a field removed and a field retyped, by its record")
{
	RegisterTypes();
	RegisterCargo();
	const std::vector<std::byte> save = SavedCargo();

	Sim::SimWorld world{kStep, 1};
	world.RegisterComponent<Position>();
	world.RegisterComponent<Cargo>(CargoMigrations());
	world.Load(save);
	const entt::registry &registry = world.Registry();
	CHECK(registry.get<Position>(entt::entity{0}).value == glm::dvec3{1.0, 2.0, 3.0});
	CHECK(registry.get<Cargo>(entt::entity{0}).tonnes == 60);
	CHECK(registry.get<Cargo>(entt::entity{0}).value == 1251);
	CHECK(registry.get<Cargo>(entt::entity{1}).tonnes == 12);
	CHECK(registry.get<Cargo>(entt::entity{1}).value == 99);
	CHECK(registry.get<Cargo>(entt::entity{2}).tonnes == 7);
	CHECK(registry.get<Cargo>(entt::entity{2}).value == 0);

	// Saved again, the storage is at version 2, and loads with no step.
	const std::vector<std::byte> again = world.Save();
	Sim::SimWorld reloaded{kStep, 1};
	reloaded.RegisterComponent<Position>();
	reloaded.RegisterComponent<Cargo>(CargoMigrations());
	reloaded.Load(again);
	CHECK(reloaded.Save() == again);

	// Without the chain, the removed field is refused by name, and the world is as it was.
	Sim::SimWorld unmigrated{kStep, 1};
	unmigrated.RegisterComponent<Position>();
	unmigrated.RegisterComponent<Cargo>();
	const std::vector<std::byte> before = unmigrated.Save();
	CHECK_THROWS_WITH_AS(
	    unmigrated.Load(save), doctest::Contains("'SimTests.Cargo' has no saved field 'grade'"),
	    Serialise::FormatError);
	CHECK(unmigrated.Save() == before);

	// A later build's version is refused by the component's name, before anything is read.
	Sim::SimWorld later{kStep, kSeed};
	Sim::ComponentMigrations third{"SimTests.CargoV3", {3}};
	third.Add(1, [](Serialise::BinaryRecord &) {}).Add(2, [](Serialise::BinaryRecord &) {});
	later.RegisterComponent<CargoV3>(std::move(third));
	later.Registry().emplace<CargoV3>(later.Registry().create(), 5u);
	Serialise::BinaryDocument document =
	    Serialise::BinaryDocument::Parse(later.Save(), "Jettison.World");
	document.Rename("SimTests.CargoV3", "SimTests.Cargo");
	Sim::SimWorld current{kStep, 1};
	current.RegisterComponent<Cargo>(CargoMigrations());
	CHECK_THROWS_WITH_AS(
	    current.Load(document.Emit()),
	    "SimTests.Cargo: version 3 found, version 2 expected: it was written by a later build",
	    Serialise::FormatVersionError);

	// A chain under another name is a mistake in the registration.
	Sim::SimWorld misnamed{kStep, 1};
	CHECK_THROWS_WITH_AS(
	    misnamed.RegisterComponent<Cargo>(Sim::ComponentMigrations{"SimTests.Hold", {2}}),
	    "SimWorld: component 'SimTests.Cargo' is given the migrations of 'SimTests.Hold'",
	    std::logic_error);
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

TEST_CASE("Sim: flight controls fly an entity only from the one at its helm, or from no one")
{
	entt::registry registry;
	const entt::entity flown = registry.create();
	const entt::entity unflown = registry.create();
	const Sim::Controller one{Sim::ControllerKind::Player, 1};
	const Sim::Controller two{Sim::ControllerKind::Player, 2};
	registry.emplace<Sim::ControlledBy>(flown, one);
	registry.emplace<Sim::ControlledBy>(unflown, one);
	registry.emplace<Sim::Helm>(flown, one);
	CHECK(Sim::MayFly(registry, flown, one));
	CHECK_FALSE(Sim::MayFly(registry, flown, two));
	// Its owner commands it, but no one is at its helm: its behaviour flies it.
	CHECK_FALSE(Sim::MayFly(registry, unflown, one));
	CHECK(Sim::MayFly(registry, unflown, {}));
	CHECK(Sim::MayFly(registry, flown, {}));
	CHECK_FALSE(Sim::MayFly(registry, entt::entity{12345}, one));
}
