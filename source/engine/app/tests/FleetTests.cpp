#include "SampleGalaxy.h"

#include "../src/Fleet.h"
#include "../src/Stations.h"
#include "../src/Traders.h"
#include "../src/Trading.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <vector>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>
#include <jettison/sim/SimWorld.h>

using namespace Jettison;
using Testing::Galaxy;

namespace
{
constexpr Sim::Controller kPlayer{Sim::ControllerKind::Player, 0};

// The player's first ship as the shell makes it (Application::SpawnShip): a shuttle on Highport's
// lowest free pad that fits it, theirs, at its helm, with the credits they start with.
entt::entity FirstShip(Galaxy &galaxy, std::int64_t credits = 250000)
{
	entt::registry &registry = galaxy.world.Registry();
	const entt::entity station = App::FindStation(registry, Sim::SystemId("Halcyon"), "Ostra Highport");
	const entt::entity ship = registry.create();
	registry.emplace<Sim::Transform>(ship, registry.get<Sim::Transform>(station));
	registry.emplace<Sim::InSystem>(ship, Sim::SystemId("Halcyon"));
	registry.emplace<Physics::LocalFrame>(ship, std::string{"Ostra Highport"});
	App::MakeShip(registry, galaxy.docks, ship, "shuttle", 2.0e4f, true);
	registry.emplace<Sim::ControlledBy>(ship, kPlayer);
	registry.emplace<App::Hold>(ship, App::Hold{20, {}});
	App::FitForTrade(registry, ship);
	registry.emplace<Sim::Helm>(ship, kPlayer);
	App::MakeOwner(registry, kPlayer, credits);
	galaxy.docks.Park(
	    galaxy.world, ship, App::FreePad(registry, station, *galaxy.docks.FindHull("shuttle")));
	return ship;
}
} // namespace

TEST_CASE(
    "Fleet: a player buys a second ship and trades from it at its helm, while the first "
    "trades on by its behaviour, and flies only the one at the helm")
{
	Galaxy galaxy;
	entt::registry &registry = galaxy.world.Registry();
	const entt::entity highport =
	    App::FindStation(registry, Sim::SystemId("Halcyon"), "Ostra Highport");
	const entt::entity first = FirstShip(galaxy);
	galaxy.Step();

	// Bought at the station the first is docked at, on its next free pad, and the helm is the new
	// ship's: the first's behaviour takes it over.
	galaxy.world.Commands().Push(App::BuyShip{kPlayer, highport, "shuttle"});
	galaxy.Step();
	const std::vector<App::ShipBought> bought = galaxy.Events<App::ShipBought>();
	REQUIRE(bought.size() == 1);
	const entt::entity second = bought[0].ship;
	CHECK(bought[0].buyer == kPlayer);
	CHECK(bought[0].price == 120000);
	CHECK(bought[0].credits == 130000);
	CHECK(App::PurseOf(registry, first)->credits == 130000);
	CHECK(Dock::IsDocked(registry, second, highport));
	CHECK(registry.get<Sim::ControlledBy>(second).controller == kPlayer);
	CHECK(registry.get<App::Hold>(second).capacity == 20);
	CHECK(registry.get<Sim::Helm>(second).controller == kPlayer);
	CHECK_FALSE(registry.all_of<Sim::Helm>(first));
	const std::vector<Ai::HelmLeft> left = galaxy.Events<Ai::HelmLeft>();
	REQUIRE(left.size() == 1);
	CHECK(left[0].ship == first);

	// Traded from at the helm: the market's cheapest good, five crates.
	const entt::entity market = galaxy.MarketAt("Ostra Highport");
	const App::MarketGood &cheapest = std::ranges::min(
	    registry.get<App::Market>(market).goods, {},
	    [](const App::MarketGood &good) { return good.ask; });
	const std::string good = cheapest.good;
	galaxy.world.Commands().Push(App::Trade{kPlayer, second, market, good, 5});
	galaxy.Step();
	const std::vector<App::Traded> traded = galaxy.Events<App::Traded>();
	REQUIRE_FALSE(traded.empty());
	CHECK(traded.back().ship == second);
	CHECK(registry.get<App::Hold>(second).Crates(good) == 5);

	// Its pilot's controls fly the ship at the helm; the first, which its behaviour flies, and the
	// second from another player, are refused.
	const Physics::FlightControls ahead{{0.0f, 0.0f, -0.5f}, {}, true, highport};
	const Sim::Controller other{Sim::ControllerKind::Player, 1};
	galaxy.world.Commands().Push(Physics::SetFlightControls{kPlayer, first, ahead});
	galaxy.world.Commands().Push(Physics::SetFlightControls{other, second, ahead});
	galaxy.world.Commands().Push(Physics::SetFlightControls{kPlayer, second, ahead});
	galaxy.Step();
	const std::vector<Physics::FlightControlsRefused> refused =
	    galaxy.Events<Physics::FlightControlsRefused>();
	REQUIRE(refused.size() == 2);
	CHECK(refused[0].ship == first);
	CHECK(refused[0].reason == Sim::MakeReason(Physics::Reason::NotAtHelm));
	CHECK(refused[1].ship == second);
	CHECK(refused[1].sender == other);
	CHECK(registry.get<Physics::FlightControls>(second) == ahead);
	galaxy.world.Commands().Push(Physics::SetFlightControls{kPlayer, second, {}});

	// Two minutes on: the first has chosen a run and left its pad by its behaviour, while the
	// second, at the helm, stands down where its pilot left it, docked.
	for (int step = 0; step < 120 * 60; ++step)
		galaxy.Step();
	CHECK_FALSE(galaxy.TraderOf(first).good.empty());
	CHECK_FALSE(Dock::IsDocked(registry, first, highport));
	CHECK(
	    glm::distance(
	        registry.get<Sim::Transform>(first).position,
	        registry.get<Sim::Transform>(highport).position) > 300.0);
	CHECK(Dock::IsDocked(registry, second, highport));
	CHECK(galaxy.TraderOf(second).good.empty());
	CHECK(registry.get<Ai::Behaviour>(second).nodes.empty());
}

TEST_CASE("Fleet: a ship is not sold to one who cannot buy it, nor where there is no pad for it")
{
	Galaxy galaxy;
	entt::registry &registry = galaxy.world.Registry();
	const entt::entity highport =
	    App::FindStation(registry, Sim::SystemId("Halcyon"), "Ostra Highport");
	const entt::entity corva = App::FindStation(registry, Sim::SystemId("Vesper"), "Corva Refinery");
	const entt::entity first = FirstShip(galaxy);
	galaxy.Step();

	const Sim::Controller stranger{Sim::ControllerKind::Player, 1};
	galaxy.world.Commands().Push(App::BuyShip{kPlayer, first, "shuttle"});
	galaxy.world.Commands().Push(App::BuyShip{kPlayer, highport, "liner"});
	galaxy.world.Commands().Push(App::BuyShip{stranger, highport, "shuttle"});
	galaxy.world.Commands().Push(App::BuyShip{{}, highport, "shuttle"});
	galaxy.world.Commands().Push(App::BuyShip{kPlayer, corva, "shuttle"});
	galaxy.world.Commands().Push(App::BuyShip{kPlayer, highport, "transport"});
	galaxy.Step();
	std::vector<std::string> reasons;
	for (const App::BuyRefused &refused : galaxy.Events<App::BuyRefused>())
		reasons.push_back(Sim::Describe(refused.reason));
	CHECK(
	    reasons == std::vector<std::string>{
	                   "reason.shell.no_station()",
	                   "reason.shell.not_for_sale(Dock.HullDefinition 'liner')",
	                   "reason.shell.no_owner()", "reason.shell.no_owner()",
	                   std::format("reason.shell.none_docked(entity {:#x})", entt::to_integral(corva)),
	                   "reason.shell.short_of_price(900000)"});
	CHECK(App::PurseOf(registry, first)->credits == 250000);
	CHECK(galaxy.Events<App::ShipBought>().empty());

	// Every free pad a shuttle fits, and then none: each bought on the next.
	std::size_t free = 0;
	for (const entt::entity pad : registry.get<Dock::Station>(highport).pads)
		free += registry.get<Dock::Pad>(pad).state == Dock::PadState::Free &&
		                !registry.get<Dock::Pad>(pad).display
		            ? 1
		            : 0;
	REQUIRE(free > 0);
	App::PurseOf(registry, first)->credits = 100000000;
	for (std::size_t buy = 0; buy <= free; ++buy)
		galaxy.world.Commands().Push(App::BuyShip{kPlayer, highport, "shuttle"});
	galaxy.events.clear();
	galaxy.Step();
	CHECK(galaxy.Events<App::ShipBought>().size() == free);
	const std::vector<App::BuyRefused> full = galaxy.Events<App::BuyRefused>();
	REQUIRE(full.size() == 1);
	CHECK(
	    Sim::Describe(full[0].reason) ==
	    std::format(
	        "reason.shell.no_pad_free(entity {:#x}, Dock.HullDefinition 'shuttle')",
	        entt::to_integral(highport)));
	// One helm, the last bought's.
	CHECK(registry.view<Sim::Helm>().size() == 1);
	CHECK(registry.get<Sim::Helm>(galaxy.Events<App::ShipBought>().back().ship).controller == kPlayer);
}

TEST_CASE("Fleet: a world saved before the helm gives the player the helm of the ship it names")
{
	Galaxy galaxy;
	entt::registry &registry = galaxy.world.Registry();
	const entt::entity ship = FirstShip(galaxy);
	// As a save from before 13d holds it: no helm, and not fitted to trade.
	registry.remove<Sim::Helm>(ship);
	registry.remove<App::Trader>(ship);
	registry.remove<Ai::Behaviour>(ship);
	registry.remove<Ai::Steered>(ship);
	CHECK(App::GiveOldShipItsHelm(registry, ship, kPlayer));
	CHECK(registry.get<Sim::Helm>(ship).controller == kPlayer);
	CHECK(registry.get<Ai::Behaviour>(ship).tree == App::kTraderTree);
	CHECK(registry.all_of<App::Trader, Ai::Steered>(ship));
	// Once only, and not for a ship another commands.
	CHECK_FALSE(App::GiveOldShipItsHelm(registry, ship, kPlayer));
	CHECK_FALSE(App::GiveOldShipItsHelm(registry, galaxy.traders.front(), kPlayer));
}

TEST_CASE("Fleet: a helm its pilot left is not given back as a save from before the helm's would be")
{
	Galaxy galaxy;
	entt::registry &registry = galaxy.world.Registry();
	const entt::entity ship = FirstShip(galaxy);
	galaxy.world.Commands().Push(Ai::LeaveHelm{kPlayer, ship});
	galaxy.Step();
	REQUIRE_FALSE(registry.all_of<Sim::Helm>(ship));
	// No helm is the player's, as in an old save, but the ship is fitted to trade, as none was then.
	CHECK_FALSE(App::GiveOldShipItsHelm(registry, ship, kPlayer));
	CHECK_FALSE(registry.all_of<Sim::Helm>(ship));
}
