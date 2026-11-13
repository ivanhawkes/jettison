#include "MarketScreen.h"
#include "Reasons.h"
#include "Trading.h"

#include <jettison/dock/Docks.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/ui/StringTable.h>
#include <jettison/ui/UiSystem.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace Jettison;
using namespace Jettison::App;

namespace
{
constexpr double kStep = 1.0 / 60.0;

MarketGood Copper(std::int64_t stock)
{
	MarketGood good{"copper", 76160, stock, 0, 1, 30, 0, 0};
	Quote(good, 150);
	return good;
}

constexpr Sim::Controller kPlayer{Sim::ControllerKind::Player, 1};

// A world with a market at a station and a ship docked on one of its pads, as the docks leave them,
// with none of physics: the trade reads only who is docked where. The ship is the player's, whose
// purse it trades with.
struct Harbour
{
	Sim::SimWorld world{kStep, 7};
	entt::entity station{entt::null};
	entt::entity pad{entt::null};
	entt::entity market{entt::null};
	entt::entity ship{entt::null};
	entt::entity owner{entt::null};
	Vfs::FileSystem assets;
	Sim::DefinitionRegistry definitions{Vfs::FileSystem{}};
	Ui::StringTable strings;

	Harbour()
	{
		Sim::RegisterTypes();
		world.RegisterComponent<Sim::ControlledBy>();
		RegisterTrading(world);
		assets.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
		strings.Load(assets, "en");
		entt::registry &registry = world.Registry();
		station = registry.create();
		pad = registry.create();
		market = registry.create();
		registry.emplace<Market>(
		    market, Market{"Ostra Highport", Sim::SystemId("Halcyon"), station, 150, {Copper(6), {}}});
		registry.get<Market>(market).goods[1] = MarketGood{"beer", 1200, 70, 2, 1, 40, 0, 0};
		Quote(registry.get<Market>(market).goods[1], 150);
		ship = registry.create();
		registry.emplace<Hold>(ship, Hold{20, {}});
		registry.emplace<Sim::ControlledBy>(ship, kPlayer);
		owner = MakeOwner(registry, kPlayer, 250000);
		registry.emplace<Dock::Docking>(ship, Dock::Docking{station, pad, Dock::Stage::Docked, false});
	}

	void Push(const std::string &good, std::int64_t crates, Sim::Controller sender = kPlayer)
	{
		world.Commands().Push(Trade{sender, ship, market, good, crates});
	}
	std::int64_t Credits() const
	{
		return world.Registry().get<Purse>(owner).credits;
	}
	// The refusals, as the player reads them in the table's language.
	std::vector<std::string> Refusals()
	{
		const Sim::Snapshot snapshot = Sim::Capture(world);
		std::vector<std::string> reasons;
		for (const Sim::SimEvent &event : world.Events().Take())
			if (const auto *refused = event.event.try_cast<TradeRefused>())
				reasons.push_back(WordReason(refused->reason, snapshot, definitions, strings));
		return reasons;
	}
	const MarketGood &Good(std::size_t index) const
	{
		return world.Registry().get<Market>(market).goods[index];
	}
};
} // namespace

TEST_CASE("Trading: a good's price follows its scarcity, and the quote is either side of it by the fee")
{
	MarketGood good = Copper(0);
	CHECK(ScarcityPrice(good) == std::int64_t{2} * 76160);
	good.stock = 30;
	CHECK(ScarcityPrice(good) == 76160);
	good.stock = std::int64_t{7} * 30;
	CHECK(ScarcityPrice(good) == 76160 / 4);
	good.stock = 1000000;
	CHECK(ScarcityPrice(good) == 76160 / 4);

	good.stock = 30;
	Quote(good, 150);
	// 1.5% each side, the ask rounded up and the bid down.
	CHECK(good.ask == 77303);
	CHECK(good.bid == 75017);
	// A cheap good never quotes nothing, and its fee is never less than a credit.
	MarketGood cheap{"grit", 1, 1000, 0, 0, 1, 0, 0};
	Quote(cheap, 150);
	CHECK(cheap.bid == 1);
	CHECK(cheap.ask == 2);
}

TEST_CASE("Trading: a docked ship buys at the ask and sells at the bid, and the price moves with it")
{
	Harbour harbour;
	const std::int64_t ask = harbour.Good(1).ask;
	harbour.Push("beer", 10);
	harbour.world.Step();
	const entt::registry &registry = harbour.world.Registry();
	CHECK(harbour.Credits() == 250000 - 10 * ask);
	CHECK(registry.get<Hold>(harbour.ship).Crates("beer") == 10);
	CHECK(harbour.Good(1).stock == 60);
	// Fewer crates on the shelf: dearer.
	CHECK(harbour.Good(1).ask > ask);

	const std::int64_t bid = harbour.Good(1).bid;
	harbour.Push("beer", -4);
	harbour.world.Step();
	CHECK(harbour.Credits() == 250000 - 10 * ask + 4 * bid);
	CHECK(registry.get<Hold>(harbour.ship).Crates("beer") == 6);
	CHECK(harbour.Good(1).stock == 64);

	std::vector<std::int64_t> traded;
	for (const Sim::SimEvent &event : harbour.world.Events().Take())
		if (const auto *fill = event.event.try_cast<Traded>())
			traded.push_back(fill->crates);
	CHECK(traded == std::vector<std::int64_t>{10, -4});

	// The last crates sold empty the hold of the good.
	harbour.Push("beer", -6);
	harbour.world.Step();
	CHECK(registry.get<Hold>(harbour.ship).cargo.empty());
}

TEST_CASE("Trading: a trade that cannot be made is refused, saying why, and changes nothing")
{
	Harbour harbour;
	entt::registry &registry = harbour.world.Registry();
	const auto refuseAll = [&harbour]
	{
		harbour.Push("copper", 7);
		harbour.Push("beer", 21);
		harbour.Push("copper", 4);
		harbour.Push("beer", -1);
		harbour.Push("salt", 1);
		harbour.Push("beer", 0);
		harbour.world.Step();
	};
	refuseAll();
	CHECK(
	    harbour.Refusals() == std::vector<std::string>{
	                              "Ostra Highport has 6 crates", "the hold has room for 20 crates",
	                              "4 crates cost more than the purse holds",
	                              "the hold has 0 crates of it", "Ostra Highport does not deal in salt",
	                              "no crates were asked for"});
	CHECK(harbour.Credits() == 250000);
	CHECK(harbour.Good(0).stock == 6);

	// The same refusals in German, from the tables: the goods by their names, the numbers grouped.
	harbour.strings.Load(harbour.assets, "de");
	refuseAll();
	CHECK(
	    harbour.Refusals() ==
	    std::vector<std::string>{
	        "Ostra Highport hat 6 Kisten", "im Laderaum ist Platz für 20 Kisten",
	        "4 Kisten kosten mehr als das Guthaben", "der Laderaum hat 0 Kisten davon",
	        "Ostra Highport handelt nicht mit salt", "es wurden keine Kisten verlangt"});
	CHECK(harbour.strings.Problems() == 0);
	harbour.strings.Load(harbour.assets, "en");

	// Coming down onto the pad, not yet clamped, or docked at another station, there is no trading.
	registry.get<Dock::Docking>(harbour.ship).stage = Dock::Stage::Descending;
	harbour.Push("beer", 1);
	harbour.world.Step();
	CHECK(harbour.Refusals() == std::vector<std::string>{"the ship is not docked at Ostra Highport"});
	const entt::entity elsewhere = registry.create();
	registry.replace<Dock::Docking>(
	    harbour.ship, Dock::Docking{elsewhere, harbour.pad, Dock::Stage::Docked, false});
	harbour.Push("beer", 1);
	harbour.world.Step();
	CHECK(harbour.Refusals() == std::vector<std::string>{"the ship is not docked at Ostra Highport"});
	CHECK(registry.get<Hold>(harbour.ship).cargo.empty());
}

TEST_CASE("Trading: a ship trades for its owner, whose purse it shares with the owner's fleet")
{
	Harbour harbour;
	entt::registry &registry = harbour.world.Registry();
	const std::int64_t ask = harbour.Good(1).ask;

	// Sent for another's ship, the trade is refused, saying so; the game's own rules may send one.
	harbour.Push("beer", 1, {Sim::ControllerKind::Player, 2});
	harbour.Push("beer", 1, {Sim::ControllerKind::Ai, 1});
	harbour.world.Step();
	CHECK(
	    harbour.Refusals() ==
	    std::vector<std::string>{
	        "its sender does not own the ship", "its sender does not own the ship"});
	CHECK(harbour.Credits() == 250000);
	CHECK(registry.get<Hold>(harbour.ship).cargo.empty());
	harbour.Push("beer", 1, {});
	harbour.world.Step();
	CHECK(harbour.Credits() == 250000 - ask);

	// A second ship of the player's fleet, docked on the same pad for the test, pays from the same
	// purse; the owner's credits outlast either ship.
	const entt::entity second = registry.create();
	registry.emplace<Hold>(second, Hold{20, {}});
	registry.emplace<Sim::ControlledBy>(second, kPlayer);
	registry.emplace<Dock::Docking>(
	    second, Dock::Docking{harbour.station, harbour.pad, Dock::Stage::Docked, false});
	const std::int64_t secondAsk = harbour.Good(1).ask;
	harbour.world.Commands().Push(Trade{kPlayer, second, harbour.market, "beer", 2});
	harbour.world.Step();
	CHECK(harbour.Credits() == 250000 - ask - 2 * secondAsk);
	CHECK(registry.get<Hold>(second).Crates("beer") == 2);
	CHECK(PurseOf(std::as_const(registry), second) == &registry.get<Purse>(harbour.owner));
	CHECK(OwnerOf(registry, harbour.ship) == harbour.owner);
	registry.destroy(harbour.ship);
	CHECK(harbour.Credits() == 250000 - ask - 2 * secondAsk);

	// A ship no one owns has no purse to trade with; and one owner a controller.
	registry.remove<Sim::ControlledBy>(second);
	harbour.world.Commands().Push(Trade{{}, second, harbour.market, "beer", 1});
	harbour.world.Step();
	CHECK(harbour.Refusals() == std::vector<std::string>{"the ship has no owner"});
	CHECK_THROWS_AS(MakeOwner(registry, kPlayer, 1), std::logic_error);
}

TEST_CASE("Trading: a world saved with purses on the ships has them moved to their owners")
{
	// The layout before phase 12c: each ship its own purse, and no owners. The purse's type is as it
	// was, so this world saves the bytes that build did.
	Harbour old;
	entt::registry &registry = old.world.Registry();
	registry.destroy(old.owner);
	registry.emplace<Purse>(old.ship, Purse{250000});
	const entt::entity second = registry.create();
	registry.emplace<Sim::ControlledBy>(second, kPlayer);
	registry.emplace<Purse>(second, Purse{1000});
	const entt::entity trader = registry.create();
	registry.emplace<Sim::ControlledBy>(trader, Sim::Controller{Sim::ControllerKind::Ai, 3});
	registry.emplace<Purse>(trader, Purse{600000});
	const std::vector<std::byte> save = old.world.Save();

	Harbour loaded;
	loaded.world.Load(save);
	entt::registry &world = loaded.world.Registry();
	CHECK(MovePursesToOwners(world) == 3);
	CHECK(!world.all_of<Purse>(old.ship));
	REQUIRE(PurseOf(std::as_const(world), old.ship));
	CHECK(PurseOf(std::as_const(world), old.ship)->credits == 251000);
	CHECK(PurseOf(std::as_const(world), trader)->credits == 600000);
	CHECK(FindOwner(world, kPlayer) != FindOwner(world, {Sim::ControllerKind::Ai, 3}));
	// Moved once: a world saved since holds its purses where they are.
	CHECK(MovePursesToOwners(world) == 0);
	// The harbour's world saves no docks, so the ship is put back on its pad.
	world.emplace<Dock::Docking>(
	    old.ship, Dock::Docking{old.station, old.pad, Dock::Stage::Docked, false});
	const std::int64_t ask = loaded.Good(1).ask;
	loaded.world.Commands().Push(Trade{kPlayer, old.ship, old.market, "beer", 1});
	loaded.world.Step();
	CHECK(PurseOf(std::as_const(world), old.ship)->credits == 251000 - ask);
}

TEST_CASE("Trading: a market makes and uses its goods each tick, up to four times its target held")
{
	Harbour harbour;
	entt::registry &registry = harbour.world.Registry();
	for (std::uint64_t step = 0; step < kMarketTickSteps - 1; ++step)
		harbour.world.Step();
	CHECK(harbour.Good(0).stock == 6);
	CHECK(harbour.Good(1).stock == 70);
	harbour.world.Step();
	CHECK(harbour.Good(0).stock == 5);
	CHECK(harbour.Good(1).stock == 71);

	// Production stops at the warehouse's limit; what a ship sells past it stays.
	registry.get<Market>(harbour.market).goods[1].stock = 200;
	TickMarkets(registry);
	CHECK(harbour.Good(1).stock == 199);
	// Use stops at none.
	registry.get<Market>(harbour.market).goods[0].stock = 0;
	TickMarkets(registry);
	CHECK(harbour.Good(0).stock == 0);
	CHECK(harbour.Good(0).ask == Copper(0).ask);
}

TEST_CASE("Trading: markets, holds and purses are saved, and the same trades give the same world")
{
	Harbour first;
	Harbour second;
	for (Harbour *harbour : {&first, &second})
	{
		harbour->Push("beer", 5);
		for (std::uint64_t step = 0; step < 3 * kMarketTickSteps; ++step)
			harbour->world.Step();
		harbour->Push("beer", -2);
		harbour->world.Step();
	}
	CHECK(first.world.StateHash() == second.world.StateHash());

	const std::vector<std::byte> save = first.world.Save();
	Harbour loaded;
	loaded.world.Load(save);
	CHECK(loaded.world.StateHash() == first.world.StateHash());
	CHECK(loaded.world.Registry().get<Hold>(first.ship).Crates("beer") == 3);
}

TEST_CASE("Trading: the market screen is made from a snapshot, never from the world")
{
	RegisterMarketScreenTypes();
	Harbour harbour;
	entt::registry &registry = harbour.world.Registry();
	const entt::entity corva = registry.create();
	MarketGood beer{"beer", 1200, 2, 0, 1, 30, 0, 0};
	Quote(beer, 120);
	registry.emplace<Market>(
	    corva, Market{"Corva Refinery", Sim::SystemId("Vesper"), entt::null, 120, {beer}});
	registry.get<Hold>(harbour.ship).cargo.push_back({"beer", 3});
	const Sim::Snapshot snapshot = Sim::Capture(harbour.world);
	// The world moving on after the snapshot is not what the screen shows.
	registry.get<Purse>(harbour.owner).credits = 0;

	const Sim::DefinitionRegistry &definitions = harbour.definitions;
	const Vfs::FileSystem &assets = harbour.assets;
	Ui::StringTable &strings = harbour.strings;
	MarketView view{harbour.market, harbour.ship, "Halcyon", true, 2, "Bought 1 crate"};
	const MarketScreen screen = BuildMarketScreen(snapshot, definitions, strings, view);
	CHECK(screen.station == "Ostra Highport");
	CHECK(screen.status == "Docked at Ostra Highport");
	CHECK(screen.credits == 250000);
	CHECK(screen.used == 3);
	CHECK(screen.capacity == 20);
	REQUIRE(screen.goods.size() == 2);
	CHECK(screen.goods[0].stock == 6);
	CHECK(screen.goods[0].ask == harbour.Good(0).ask);
	// Two crates of copper, the lot, cost more than the ship has; it has beer to sell.
	CHECK_FALSE(screen.goods[0].canBuy);
	CHECK(screen.goods[1].canSell);
	CHECK(screen.goods[1].held == 3);
	CHECK(screen.goods[1].elsewhere == "Corva Refinery pays " + FormatCredits(beer.bid));
	CHECK(strings.Number(1250000) == "1,250,000");
	CHECK(screen.message == "Bought 1 crate");

	// The goods are named by the table, without their definitions.
	CHECK(screen.goods[1].name == "Beer");
	CHECK(screen.goods[1].crate == "a kilolitre of beer in casks");

	view.docked = false;
	const MarketScreen away = BuildMarketScreen(snapshot, definitions, strings, view);
	CHECK(away.status == "Dock at Ostra Highport to trade");
	CHECK_FALSE(away.goods[1].canSell);

	// And in German, the stations' names as they are.
	strings.Load(assets, "de");
	CHECK(strings.Problems() == 0);
	const MarketScreen german = BuildMarketScreen(snapshot, definitions, strings, view);
	CHECK(german.status == "Zum Handeln an Ostra Highport andocken");
	CHECK(german.goods[1].name == "Bier");
	CHECK(german.goods[1].elsewhere == "Corva Refinery zahlt " + strings.Number(beer.bid));
	CHECK(strings.Number(-1250000) == "-1.250.000");
	CHECK(
	    strings.Plural("market.bought", 1, "Bier", strings.Number(1200)) ==
	    "1 Kiste Bier zu 1.200 je Kiste gekauft");
	view.market = entt::null;
	CHECK(
	    BuildMarketScreen(snapshot, definitions, strings, view).status ==
	    "In diesem System gibt es keinen Markt");
	CHECK(strings.Problems() == 0);
	CHECK(FormatCredits(1250000) == "1,250,000");
	CHECK(FormatCredits(-999) == "-999");
}

TEST_CASE("Trading: the sample game's goods and markets load, and every market's goods are defined")
{
	Vfs::FileSystem files;
	files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
	Sim::DefinitionRegistry definitions{files};
	LoadTradeDefinitions(definitions);
	CHECK(definitions.Names<Good>().size() == 8);
	REQUIRE(
	    definitions.Names<MarketDefinition>() ==
	    std::vector<std::string>{"corva-refinery", "ostra-highport"});
	CHECK(definitions.Find<Good>("copper")->basePrice == 76160);

	Sim::SimWorld world{kStep, 1};
	Sim::RegisterTypes();
	RegisterTrading(world);
	const std::vector<entt::entity> markets = OpenMarkets(world, definitions);
	REQUIRE(markets.size() == 2);
	for (const entt::entity entity : markets)
	{
		const Market &market = world.Registry().get<Market>(entity);
		CHECK(market.goods.size() == 8);
		for (const MarketGood &good : market.goods)
			CHECK(good.ask > good.bid);
	}
	CHECK(world.Registry().get<Market>(markets[1]).station == "Ostra Highport");
	CHECK(world.Registry().get<Market>(markets[1]).system == Sim::SystemId("Halcyon"));
}

TEST_CASE(
    "Trading: the market document shows the screen's data, and its buttons raise the shell's events")
{
	RegisterMarketScreenTypes();
	Vfs::FileSystem files;
	files.AddMount({"base", JETTISON_ASSET_SOURCE_DIR, false});
	Ui::UiSystem ui{files, 1920, 1080};
	ui.LoadFont("fonts/Roboto-Medium.ttf");
	ui.LoadFont("fonts/Hack-Regular.ttf");
	ui.SetLanguage("en");

	MarketScreen screen;
	screen.station = "Ostra Highport";
	screen.docked = true;
	screen.credits = 250000;
	screen.goods.push_back({"beer", "Beer", "a kilolitre", 1200, 57, 974, 1004, 2, "", true, true});
	std::vector<std::string> raised;
	Ui::DataModel &model = ui.CreateDataModel("market");
	model.Bind("screen", &screen);
	const auto record = [&raised](std::string name)
	{
		return [&raised, name](std::span<const Ui::EventArgument> arguments)
		{
			std::string line = name;
			for (const Ui::EventArgument &argument : arguments)
				line += " " + Ui::AsText(argument);
			raised.push_back(line);
		};
	};
	model.BindEvent("buy", record("buy"));
	model.BindEvent("sell", record("sell"));
	model.BindEvent("lot", record("lot"));
	const Ui::DocumentHandle document = ui.LoadDocument("ui/market.rml");
	ui.Show(document);
	ui.Update(0.0);

	CHECK(ui.ElementText(document, "station") == "Ostra Highport");
	CHECK(ui.ElementText(document, "credits") == "250,000");
	CHECK(ui.ElementText(document, "ask-0") == "1,004");
	CHECK(ui.ElementText(document, "footer") == "M closes the market.");
	CHECK(ui.Click(document, "lot-10"));
	screen.lot = 10;
	model.Changed("screen");
	ui.Update(0.1);
	CHECK(ui.Click(document, "buy-0"));
	CHECK(ui.Click(document, "sell-0"));
	ui.Update(0.2);
	CHECK(raised == std::vector<std::string>{"lot 10", "buy beer 10", "sell beer 10"});

	// The mouse, as the shell feeds it a frame at a time: one click is one trade, however many frames
	// the cursor rests on the button after it.
	raised.clear();
	constexpr glm::vec2 kBuy{1800.0f, 155.0f};
	ui.MouseMove(kBuy, {});
	ui.Update(0.3);
	ui.MouseMove(kBuy, {});
	ui.MouseButton(0, true, {});
	ui.MouseButton(0, false, {});
	for (int frame = 0; frame < 5; ++frame)
	{
		screen.goods[0].stock -= 1;
		model.Changed("screen");
		ui.MouseMove(kBuy, {});
		ui.Update(0.4 + frame * 0.1);
	}
	CHECK(raised == std::vector<std::string>{"buy beer 10"});

	// The document is the frame's size, and the mouse goes through it but for its panel, at the
	// right: over the scene at the left it is the scene's.
	ui.MouseMove({200.0f, 500.0f}, {});
	CHECK_FALSE(ui.WantsMouse());
	ui.MouseMove({1500.0f, 100.0f}, {});
	CHECK(ui.WantsMouse());
	CHECK(ui.Problems() == 0);
}
