#include "../src/Saves.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <jettison/celestial/Galaxy.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;

TEST_CASE("Saves: a save's name is one segment of a path, and the shell's state reads back")
{
	CHECK(App::SavePath("quicksave") == "saves/quicksave.save");
	CHECK(App::SavePath("before-the-jump_2") == "saves/before-the-jump_2.save");
	for (const char *name : {"", "../settings", "a/b", "a b", "a:b", "a.b"})
		CHECK_THROWS_AS(App::SavePath(name), std::invalid_argument);
	CHECK_THROWS_AS(App::SavePath(std::string(65, 'a')), std::invalid_argument);

	const App::ShellState state{entt::entity{3}, entt::entity{1}, 0x1234, "Highport"};
	const App::ShellState read = App::ReadShellState(App::WriteShellState(state));
	CHECK(read.ship == state.ship);
	CHECK(read.station == state.station);
	CHECK(read.shipSystem == 0x1234);
	CHECK(read.stationBody == "Highport");
	CHECK_THROWS_AS(App::ReadShellState({}), Serialise::FormatError);
}

TEST_CASE("Saves: the saves are the user mount's, by name")
{
	const auto directory =
	    std::filesystem::temp_directory_path() / ("jettison-saves-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(directory, error);
	std::filesystem::create_directories(directory / "base");
	Vfs::FileSystem files;
	files.AddMount({"base", directory / "base"});
	files.AddMount({"user", directory / "user", true});
	CHECK(App::ListSaves(files).empty());
	const std::vector<std::byte> bytes{std::byte{1}};
	files.Write(App::SavePath("second"), bytes);
	files.Write(App::SavePath("first"), bytes);
	// Not saves: another extension, and a name no save could have.
	files.Write("saves/notes.txt", bytes);
	files.Write("saves/a.b.save", bytes);
	CHECK(App::ListSaves(files) == std::vector<std::string>{"first", "second"});

	// The load screen lists them, and a file that is not a save says why.
	Sim::SaveInfo info;
	info.name = "first";
	info.engineVersion = "0.1.0";
	info.writtenAt = 1794476280;
	info.step = 1200;
	info.system = "Halcyon";
	Sim::SaveGame save;
	save.info = info;
	files.Write(App::SavePath("first"), save.Write());
	const App::SavesScreen screen = App::BuildSavesScreen(files);
	REQUIRE(screen.saves.size() == 2);
	CHECK(screen.saves[0].name == "first");
	CHECK(screen.saves[0].system == "Halcyon");
	CHECK(screen.saves[0].written == "2026-11-12 09:38");
	CHECK(screen.saves[0].step == 1200);
	CHECK(screen.saves[0].engine == "0.1.0");
	CHECK(screen.saves[0].problem.empty());
	CHECK(screen.saves[1].name == "second");
	CHECK_FALSE(screen.saves[1].problem.empty());
	std::filesystem::remove_all(directory);
}

TEST_CASE("Saves: the systems a world's entities are in that its galaxy does not have are named")
{
	Sim::RegisterTypes();
	Celestial::Galaxy galaxy;
	galaxy.systems = {{"Halcyon", "systems/halcyon.yaml", {}}, {"Vesper", "systems/vesper.yaml", {}}};
	entt::registry world;
	world.emplace<Sim::InSystem>(world.create(), Sim::SystemId("Halcyon"));
	world.emplace<Sim::InSystem>(world.create(), Sim::kNoSystem);
	CHECK(App::UnknownSystems(world, galaxy).empty());
	world.emplace<Sim::InSystem>(world.create(), Sim::SystemId("Gone"));
	world.emplace<Sim::InSystem>(world.create(), Sim::SystemId("Gone"));
	CHECK(App::UnknownSystems(world, galaxy) == std::vector<std::uint64_t>{Sim::SystemId("Gone")});
}
