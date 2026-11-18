#include "Application.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

#include <jettison/reflect/Reflect.h>

using namespace Jettison;


TEST_CASE("Game: the shell with no game composed into it registers no market, trader or fleet")
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
