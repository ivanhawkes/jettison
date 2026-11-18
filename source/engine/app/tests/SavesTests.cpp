#include "../src/Saves.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <jettison/sim/SaveGame.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;


TEST_CASE("Saves: a save's name is one segment of a path")
{
	CHECK(App::SavePath("quicksave") == "saves/quicksave.save");
	CHECK(App::SavePath("before-the-jump_2") == "saves/before-the-jump_2.save");
	for (const char *name : {"", "../settings", "a/b", "a b", "a:b", "a.b"})
		CHECK_THROWS_AS(App::SavePath(name), std::invalid_argument);
	CHECK_THROWS_AS(App::SavePath(std::string(65, 'a')), std::invalid_argument);
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
	info.details = {{std::string{App::kPlaceDetail}, "Halcyon"}};
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
