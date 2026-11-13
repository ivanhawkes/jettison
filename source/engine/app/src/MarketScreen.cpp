#include "MarketScreen.h"

#include <format>
#include <string>

#include <jettison/dock/Docks.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/ui/StringTable.h>

#include "Trading.h"

namespace Jettison::App
{
void RegisterMarketScreenTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<MarketRow>("Shell.MarketRow")
		    .Field<&MarketRow::good>("good")
		    .Field<&MarketRow::name>("name")
		    .Field<&MarketRow::crate>("crate")
		    .Field<&MarketRow::base>("base")
		    .Field<&MarketRow::stock>("stock")
		    .Field<&MarketRow::bid>("bid")
		    .Field<&MarketRow::ask>("ask")
		    .Field<&MarketRow::held>("held")
		    .Field<&MarketRow::elsewhere>("elsewhere")
		    .Field<&MarketRow::canBuy>("canBuy")
		    .Field<&MarketRow::canSell>("canSell");
		RegisterType<MarketScreen>("Shell.MarketScreen")
		    .Field<&MarketScreen::station>("station")
		    .Field<&MarketScreen::system>("system")
		    .Field<&MarketScreen::docked>("docked")
		    .Field<&MarketScreen::status>("status")
		    .Field<&MarketScreen::credits>("credits")
		    .Field<&MarketScreen::used>("used")
		    .Field<&MarketScreen::capacity>("capacity")
		    .Field<&MarketScreen::lot>("lot")
		    .Field<&MarketScreen::goods>("goods")
		    .Field<&MarketScreen::message>("message");
		return true;
	}();
	(void)registered;
}

std::string FormatCredits(std::int64_t credits)
{
	std::string digits = std::to_string(credits < 0 ? -credits : credits);
	for (auto at = static_cast<std::ptrdiff_t>(digits.size()) - 3; at > 0; at -= 3)
		digits.insert(static_cast<std::size_t>(at), ",");
	return credits < 0 ? "-" + digits : digits;
}

std::string GoodName(
    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings, std::string_view good)
{
	if (const auto name = strings.Find(std::format("goods.{}.name", good)))
		return std::string{*name};
	const Good *definition = definitions.Find<Good>(good);
	return definition ? definition->displayName : std::string{good};
}

MarketScreen BuildMarketScreen(
    const Sim::Snapshot &snapshot, const Sim::DefinitionRegistry &definitions,
    const Ui::StringTable &strings, const MarketView &view)
{
	MarketScreen screen;
	screen.system = view.system;
	screen.docked = view.docked;
	screen.lot = view.lot;
	screen.message = view.message;
	const auto *market = snapshot.Get<Market>(view.market);
	if (!market)
	{
		screen.station = strings.Text("market.none.station");
		screen.status = strings.Text("market.none.status");
		return screen;
	}
	screen.station = market->station;
	screen.status = view.docked ? strings.Format("market.docked", market->station)
	                : market->stationEntity != entt::null
	                    ? strings.Format("market.dock-to-trade", market->station)
	                    : std::string{strings.Text("market.no-dock")};
	// What is still crossing the deck holds the ship on its pad.
	if (const auto *transfer = view.docked ? snapshot.Get<Dock::Transfer>(view.ship) : nullptr)
	{
		std::int64_t crossing = transfer->aboard + transfer->ashore;
		for (const Sim::ComponentSample<Dock::Buggy> &buggy : snapshot.Components<Dock::Buggy>())
			if (buggy.value.ship == view.ship && buggy.value.outbound)
				crossing += buggy.value.crates;
		if (crossing > 0)
			screen.status += strings.Plural("market.crossing", crossing);
	}
	const auto *hold = snapshot.Get<Hold>(view.ship);
	const auto *purse = snapshot.Get<Purse>(view.ship);
	if (hold)
	{
		screen.used = hold->Used();
		screen.capacity = hold->capacity;
	}
	if (purse)
		screen.credits = purse->credits;

	for (const MarketGood &good : market->goods)
	{
		const Good *definition = definitions.Find<Good>(good.good);
		MarketRow row;
		row.good = good.good;
		row.name = GoodName(definitions, strings, good.good);
		const auto crate = strings.Find(std::format("goods.{}.crate", good.good));
		row.crate = crate ? std::string{*crate} : definition ? definition->crate : std::string{};
		row.base = good.basePrice;
		row.stock = good.stock;
		row.bid = good.bid;
		row.ask = good.ask;
		row.held = hold ? hold->Crates(good.good) : 0;
		// The other markets in snapshot order, which is the entities' order, so a tie goes the same
		// way every run.
		std::int64_t bestBid = 0;
		std::string bestAt;
		for (const auto &other : snapshot.Components<Market>())
		{
			if (other.entity == view.market)
				continue;
			for (const MarketGood &theirs : other.value.goods)
				if (theirs.good == good.good && theirs.bid > bestBid)
				{
					bestBid = theirs.bid;
					bestAt = other.value.station;
				}
		}
		if (bestBid > 0)
			row.elsewhere = strings.Format("market.pays", bestAt, strings.Number(bestBid));
		row.canBuy = view.docked && good.stock >= view.lot &&
		             screen.capacity - screen.used >= view.lot && screen.credits >= good.ask * view.lot;
		row.canSell = view.docked && row.held >= view.lot;
		screen.goods.push_back(std::move(row));
	}
	return screen;
}
} // namespace Jettison::App
