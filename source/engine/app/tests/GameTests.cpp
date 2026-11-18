#include "Application.h"

#include <doctest/doctest.h>

#include <array>
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
