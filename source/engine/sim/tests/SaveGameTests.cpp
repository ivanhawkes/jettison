#include <jettison/sim/SaveGame.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>
#include <jettison/sim/SimWorld.h>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison;
using Reflect::Attribute;

namespace
{
// A world of freighters that load a tonne a step, and a game that keeps the player's credits
// outside the world, in a section of its own.
struct Freight
{
	std::uint32_t tonnes{0};
};
struct Purse
{
	std::int64_t credits{0};
};

void RegisterTypes()
{
	static const bool registered = []
	{
		Reflect::RegisterType<Freight>("SaveGameTests.Freight")
		    .Field<&Freight::tonnes>("tonnes", Attribute::Saved);
		Reflect::RegisterType<Purse>("SaveGameTests.Purse")
		    .Field<&Purse::credits>("credits", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

constexpr double kStep = 1.0 / 60.0;

void Populate(Sim::SimWorld &world)
{
	world.RegisterComponent<Freight>();
	world.RegisterSystem(
	    {"load",
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<>().Writing<Freight>(),
	     {},
	     [](entt::registry &registry, double)
	     {
		     for (auto [entity, freight] : registry.view<Freight>().each())
			     freight.tonnes += 1;
	     }});
}

// Three freighters, the middle one scrapped, stepped ten times.
void Play(Sim::SimWorld &world)
{
	entt::registry &registry = world.Registry();
	for (std::uint32_t tonnes : {5u, 7u, 11u})
		registry.emplace<Freight>(registry.create(), tonnes);
	registry.destroy(entt::entity{1});
	for (int step = 0; step < 10; ++step)
		world.Step();
}

Sim::SaveGame Saved(const Sim::SimWorld &world)
{
	Sim::SaveGame save = Sim::SaveGame::Of(world, "Before the jump");
	save.info.writtenAt = 1'794'400'000;
	save.info.galaxy = "systems/galaxy.yaml";
	save.info.system = "Halcyon";
	save.AddSection("SaveGameTests.Purse", Serialise::BinaryArchive::Write(Purse{125'000}, {1}));
	return save;
}

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

TEST_CASE("SaveGame: a save holds its info, the world and the game's sections, and reads back")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, 7};
	Populate(world);
	Play(world);
	const Sim::SaveGame save = Saved(world);
	CHECK(save.info.step == 10);
	CHECK(save.info.time == world.Clock().Now());
	CHECK_FALSE(save.info.engineVersion.empty());

	const std::vector<std::byte> bytes = save.Write();
	const Sim::SaveGame read = Sim::SaveGame::Read(bytes);
	CHECK(read.info.name == "Before the jump");
	CHECK(read.info.writtenAt == 1'794'400'000);
	CHECK(read.info.galaxy == "systems/galaxy.yaml");
	CHECK(read.info.system == "Halcyon");
	CHECK(read.world == world.Save());
	REQUIRE(read.FindSection("SaveGameTests.Purse"));
	CHECK(
	    Serialise::BinaryArchive::Read<Purse>(read.FindSection("SaveGameTests.Purse")->data, {1})
	        .credits == 125'000);
	CHECK_FALSE(read.FindSection("SaveGameTests.Other"));
	CHECK(read.Write() == bytes);

	// The info alone, for a list of saves.
	CHECK(Sim::SaveGame::ReadInfo(bytes).name == "Before the jump");

	// The world loads from it into a world registered the same way, and steps on as the saved one.
	Sim::SimWorld loaded{kStep, 1};
	Populate(loaded);
	loaded.Load(read.world);
	world.Step();
	loaded.Step();
	CHECK(loaded.Save() == world.Save());
}

TEST_CASE("SaveGame: a save that does not fit is refused, whatever is cut from it")
{
	RegisterTypes();
	Sim::SimWorld world{kStep, 7};
	Populate(world);
	Play(world);
	const std::vector<std::byte> bytes = Saved(world).Write();
	for (std::size_t size = 0; size < bytes.size(); ++size)
		CHECK_THROWS_AS(Sim::SaveGame::Read(std::span{bytes}.first(size)), Serialise::FormatError);
	std::vector<std::byte> longer = bytes;
	longer.push_back(std::byte{0});
	CHECK_THROWS_AS(Sim::SaveGame::Read(longer), Serialise::FormatError);
	// A world's save is not a save game.
	CHECK_THROWS_AS(Sim::SaveGame::Read(world.Save()), Serialise::FormatError);

	Sim::SaveGame save = Saved(world);
	CHECK_THROWS_AS(save.AddSection("SaveGameTests.Purse", {}), std::logic_error);
	CHECK_THROWS_AS(save.AddSection("", {}), std::logic_error);

	// One from a later build is refused by version, before anything is read.
	Serialise::BinaryDocument document = Serialise::BinaryDocument::Parse(bytes, "Jettison.Save");
	document.version = Sim::SaveGame::kVersion.version + 1;
	CHECK_THROWS_AS(Sim::SaveGame::Read(document.Emit()), Serialise::FormatVersionError);
	CHECK_THROWS_AS(Sim::SaveGame::ReadInfo(document.Emit()), Serialise::FormatVersionError);
}

// The save game's first version, as phase 8a's build wrote it (Saved, above): a save game at version
// 1 holding a world at version 2. Every later build loads it, through whatever migrations the save
// game and the world have gained by then.
TEST_CASE("SaveGame: a save written by version 1 loads")
{
	RegisterTypes();
	const std::vector<std::byte> fixture = ReadFixture("save-v1.save");
	CHECK(Serialise::PeekVersion(fixture, "Jettison.Save") == 1);
	const Sim::SaveGame save = Sim::SaveGame::Read(fixture);
	CHECK(save.info.name == "Before the jump");
	CHECK(save.info.engineVersion == "0.1.0");
	CHECK(save.info.step == 10);
	CHECK(save.info.system == "Halcyon");
	CHECK(
	    Serialise::BinaryArchive::Read<Purse>(save.FindSection("SaveGameTests.Purse")->data, {1})
	        .credits == 125'000);

	Sim::SimWorld loaded{kStep, 1};
	Populate(loaded);
	loaded.Load(save.world);
	CHECK(loaded.Clock().Step() == 10);
	CHECK(loaded.Registry().get<Freight>(entt::entity{0}).tonnes == 15);
	CHECK(loaded.Registry().get<Freight>(entt::entity{2}).tonnes == 21);
	CHECK_FALSE(loaded.Registry().valid(entt::entity{1}));

	// It is the state this build reaches, saved as this build saves it.
	Sim::SimWorld current{kStep, 7};
	Populate(current);
	Play(current);
	CHECK(loaded.Save() == current.Save());
}
