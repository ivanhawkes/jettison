#include "MarketScreen.h"

#include <format>
#include <string>

#include <jettison/reflect/Reflect.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Snapshot.h>

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

MarketScreen BuildMarketScreen(
    const Sim::Snapshot &snapshot, const Sim::DefinitionRegistry &definitions, const MarketView &view)
{
	MarketScreen screen;
	screen.system = view.system;
	screen.docked = view.docked;
	screen.lot = view.lot;
	screen.message = view.message;
	const auto *market = snapshot.Get<Market>(view.market);
	if (!market)
	{
		screen.station = "No market";
		screen.status = "There is no market in this system";
		return screen;
	}
	screen.station = market->station;
	screen.status = view.docked                           ? "Docked at " + market->station
	                : market->stationEntity != entt::null ? "Dock at " + market->station + " to trade"
	                                                      : "No ship can dock here yet";
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
		row.name = definition ? definition->displayName : good.good;
		row.crate = definition ? definition->crate : std::string{};
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
			row.elsewhere = std::format("{} pays {}", bestAt, FormatCredits(bestBid));
		row.canBuy = view.docked && good.stock >= view.lot &&
		             screen.capacity - screen.used >= view.lot && screen.credits >= good.ask * view.lot;
		row.canSell = view.docked && row.held >= view.lot;
		screen.goods.push_back(std::move(row));
	}
	return screen;
}
} // namespace Jettison::App
