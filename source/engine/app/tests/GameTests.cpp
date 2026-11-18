#include "Application.h"

#include <doctest/doctest.h>

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/ecs/Scheduler.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>

using namespace Jettison;

namespace Jettison::App
{
// The shell's stages that need no window: its user directory, resources and scene, its saves, and
// its frame's order.
struct ApplicationProbe
{
	static void Start(Application &shell)
	{
		shell.InitUser();
		shell.InitResources();
		shell.InitScene();
	}


	static void Reload(Application &shell)
	{
		shell.ReloadScene();
	}


	static bool Save(Application &shell, std::string_view name)
	{
		return shell.SaveTo(name);
	}


	static bool Load(Application &shell, std::string_view name)
	{
		return shell.LoadFrom(name);
	}


	static std::vector<std::string> Order(const Application &shell, Ecs::TickPhase phase)
	{
		return shell.m_frame.Order(phase);
	}


	static Sim::SimWorld &World(Application &shell)
	{
		return shell.m_sim;
	}
};
} // namespace Jettison::App

namespace
{
// A game of no genre on the shell's seams alone: a frame system after the shell's sync, a save section
// and detail of its own, and the scenes it is shown.
class SeamGame final : public App::Game
{
  public:
	void RegisterFrame(Ecs::Scheduler &frame, App::Shell & /*shell*/) override
	{
		frame.Register(
		    {"test.frame", Ecs::TickPhase::Sync, {}, {after}, [](entt::registry &, double) {}});
	}


	void SceneLoaded(App::Shell & /*shell*/) override
	{
		++loaded;
	}


	void SceneUnloading(App::Shell & /*shell*/) override
	{
		++unloading;
	}


	void WriteSave(Sim::SaveGame &save, App::Shell & /*shell*/) override
	{
		save.info.details.push_back({"test.owner", owner});
		save.AddSection("Test.Game", section);
	}


	void ReadSave(const Sim::SaveGame &save, App::Shell & /*shell*/) override
	{
		if (save.info.Detail("test.owner") != owner)
			throw std::runtime_error("not this game's save");
		const Sim::SaveSection *found = save.FindSection("Test.Game");
		read = found ? found->data : std::vector<std::byte>{};
	}

	std::string after{"sim.sync"};
	std::string owner{"seams"};
	std::vector<std::byte> section{std::byte{7}, std::byte{0}, std::byte{42}};
	std::vector<std::byte> read;
	int loaded{0};
	int unloading{0};
};


App::AppArguments WindowlessArguments(const std::filesystem::path &user)
{
	App::AppArguments arguments;
	arguments.assets = JETTISON_ASSET_SOURCE_DIR;
	arguments.userDirectory = user;
	arguments.noSound = true;
	return arguments;
}
} // namespace


TEST_CASE("Game: the shell with no game composed into it registers no market, trader, fleet or ship")
{
	// The shell as Flotsam makes it: its world's systems attached, its commands and console's, and no
	// game. Nothing of the space-trading sample is in this suite's link, so a type of the sample's
	// that resolved would be the shell's own registering it.
	const App::Application shell{App::AppArguments{}, "Neutral"};
	constexpr std::array<std::string_view, 9> kSample{
	    "Shell.Market", "Shell.MarketDefinition", "Shell.Good",
	    "Shell.Trade",  "Shell.Trader",           "Shell.Hold",
	    "Shell.Purse",  "Shell.BuyShip",          "Shell.ShipForSale"};
	for (const std::string_view name : kSample)
	{
		CAPTURE(name);
		CHECK_FALSE(Reflect::Resolve(name));
	}
	// The shell's own world is registered all the same.
	CHECK(Reflect::Resolve("Sim.Transform"));
	// And it makes no ship, for the player or as traffic, of a hull no content need define.
	CHECK_FALSE(App::Game{}.StarterShip());
}


TEST_CASE("Game: the engine's assets are the engine's, and none of the sample's content is in them")
{
	// What assets/CMakeLists.txt stages and packs: CMakeLists.txt itself is not staged.
	constexpr std::array<std::string_view, 4> kEngine{"fonts", "shaders", "strings", "ui"};
	const std::filesystem::path engine{JETTISON_ASSET_SOURCE_DIR};
	const std::filesystem::path sample{JETTISON_SAMPLE_ASSET_DIR};
	std::size_t files = 0;
	for (const auto &entry : std::filesystem::recursive_directory_iterator{engine})
	{
		if (!entry.is_regular_file() || entry.path().filename() == "CMakeLists.txt")
			continue;
		const std::filesystem::path path = entry.path().lexically_relative(engine);
		CAPTURE(path.string());
		CHECK(std::ranges::find(kEngine, path.begin()->string()) != kEngine.end());
		// A file of the engine's the sample ships again would be the sample's to change, not the
		// engine's: an engine path is the sample's at most to read.
		CHECK_FALSE(std::filesystem::exists(sample / path));
		++files;
	}
	CHECK(files > 0);
}


TEST_CASE("Game: a game's frame system, save section and scene hooks run on the shell's seams")
{
	const auto user =
	    std::filesystem::temp_directory_path() / ("jettison-game-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(user, error);
	SeamGame game;
	{
		App::Application shell{WindowlessArguments(user), "Seams", &game};

		// In the phase it asked for, after the system it named.
		const std::vector<std::string> sync = App::ApplicationProbe::Order(shell, Ecs::TickPhase::Sync);
		const auto named = std::ranges::find(sync, "sim.sync");
		const auto own = std::ranges::find(sync, "test.frame");
		REQUIRE(named != sync.end());
		REQUIRE(own != sync.end());
		CHECK(named < own);

		App::ApplicationProbe::Start(shell);
		CHECK(game.loaded == 1);
		CHECK(game.unloading == 0);
		App::ApplicationProbe::Reload(shell);
		CHECK(game.loaded == 2);
		CHECK(game.unloading == 1);

		// The section goes out and comes back as it was written.
		REQUIRE(App::ApplicationProbe::Save(shell, "seams"));
		Sim::SimWorld &world = App::ApplicationProbe::World(shell);
		const std::uint64_t saved = world.StateHash();
		world.Step();
		REQUIRE(world.StateHash() != saved);
		REQUIRE(App::ApplicationProbe::Load(shell, "seams"));
		CHECK(game.read == game.section);
		CHECK(world.StateHash() == saved);

		// A save the game refuses leaves the world as it was.
		world.Step();
		const std::uint64_t stepped = world.StateHash();
		game.owner = "another";
		game.read.clear();
		CHECK_FALSE(App::ApplicationProbe::Load(shell, "seams"));
		CHECK(game.read.empty());
		CHECK(world.StateHash() == stepped);
	}
	std::filesystem::remove_all(user, error);
}


TEST_CASE("Game: a game's frame system naming no system of the shell's fails the shell's start")
{
	SeamGame game;
	game.after = "no.such.system";
	CHECK_THROWS_AS(App::Application(App::AppArguments{}, "Seams", &game), std::logic_error);
}
