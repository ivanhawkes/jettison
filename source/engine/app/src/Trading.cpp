#include "Trading.h"

#include <algorithm>
#include <format>
#include <limits>
#include <string>
#include <utility>

#include <jettison/core/Log.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Docking.h"

namespace Jettison::App
{
namespace
{
constexpr Core::Category kTrading{"trading"};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<Good>("Shell.Good")
		    .Field<&Good::displayName>("displayName", Attribute::Saved)
		    .Field<&Good::category>("category", Attribute::Saved)
		    .Field<&Good::age>("age", Attribute::Saved)
		    .Field<&Good::crate>("crate", Attribute::Saved)
		    .Field<&Good::basePrice>("basePrice", Attribute::Saved);
		RegisterType<MarketListing>("Shell.MarketListing")
		    .Field<&MarketListing::good>("good", Attribute::Saved)
		    .Field<&MarketListing::stock>("stock", Attribute::Saved)
		    .Field<&MarketListing::production>("production", Attribute::Saved)
		    .Field<&MarketListing::consumption>("consumption", Attribute::Saved)
		    .Field<&MarketListing::target>("target", Attribute::Saved);
		RegisterType<MarketDefinition>("Shell.MarketDefinition")
		    .Field<&MarketDefinition::station>("station", Attribute::Saved)
		    .Field<&MarketDefinition::system>("system", Attribute::Saved)
		    .Field<&MarketDefinition::feeBps>("feeBps", Attribute::Saved)
		    .Field<&MarketDefinition::goods>("goods", Attribute::Saved);
		RegisterType<MarketGood>("Shell.MarketGood")
		    .Field<&MarketGood::good>("good", Attribute::Saved)
		    .Field<&MarketGood::basePrice>("basePrice", Attribute::Saved)
		    .Field<&MarketGood::stock>("stock", Attribute::Saved)
		    .Field<&MarketGood::production>("production", Attribute::Saved)
		    .Field<&MarketGood::consumption>("consumption", Attribute::Saved)
		    .Field<&MarketGood::target>("target", Attribute::Saved)
		    .Field<&MarketGood::bid>("bid", Attribute::Saved)
		    .Field<&MarketGood::ask>("ask", Attribute::Saved);
		RegisterType<Market>("Shell.Market")
		    .Field<&Market::station>("station", Attribute::Saved)
		    .Field<&Market::system>("system", Attribute::Saved)
		    .Field<&Market::stationEntity>("stationEntity", Attribute::Saved)
		    .Field<&Market::feeBps>("feeBps", Attribute::Saved)
		    .Field<&Market::goods>("goods", Attribute::Saved);
		RegisterType<Cargo>("Shell.Cargo")
		    .Field<&Cargo::good>("good", Attribute::Saved)
		    .Field<&Cargo::crates>("crates", Attribute::Saved);
		RegisterType<Hold>("Shell.Hold")
		    .Field<&Hold::capacity>("capacity", Attribute::Saved)
		    .Field<&Hold::cargo>("cargo", Attribute::Saved);
		RegisterType<Purse>("Shell.Purse").Field<&Purse::credits>("credits", Attribute::Saved);
		RegisterType<Trade>("Shell.Trade")
		    .Field<&Trade::ship>("ship", Attribute::Saved)
		    .Field<&Trade::market>("market", Attribute::Saved)
		    .Field<&Trade::good>("good", Attribute::Saved)
		    .Field<&Trade::crates>("crates", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

void Refuse(Sim::SimWorld &world, const Trade &trade, std::string reason)
{
	world.Events().Emit(TradeRefused{trade.ship, trade.good, std::move(reason)});
}

MarketGood *FindGood(Market &market, std::string_view good)
{
	const auto found = std::ranges::find(market.goods, good, &MarketGood::good);
	return found != market.goods.end() ? &*found : nullptr;
}

// What crates of a good cost at a price, if it fits in a 64-bit count of credits.
bool Cost(std::int64_t crates, std::int64_t price, std::int64_t &cost)
{
	if (price != 0 && crates > std::numeric_limits<std::int64_t>::max() / price)
		return false;
	cost = crates * price;
	return true;
}
} // namespace

std::int64_t Hold::Used() const
{
	std::int64_t used = 0;
	for (const Cargo &held : cargo)
		used += held.crates;
	return used;
}

std::int64_t Hold::Crates(std::string_view good) const
{
	const auto found = std::ranges::find(cargo, good, &Cargo::good);
	return found != cargo.end() ? found->crates : 0;
}

void RegisterTrading(Sim::SimWorld &world)
{
	RegisterTypes();
	world.RegisterComponent<Market>();
	world.RegisterComponent<Hold>();
	world.RegisterComponent<Purse>();
	world.ShowInSnapshots<Market>();
	world.ShowInSnapshots<Hold>();
	world.ShowInSnapshots<Purse>();
	world.RegisterCommand<Trade>([](Sim::SimWorld &target, const Trade &trade)
	                             { ApplyTrade(target, trade); });
	world.RegisterSystem(
	    {std::string{kMarketSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<>().Writing<Market>(),
	     {},
	     [&world](entt::registry &registry, double)
	     {
		     if (world.Clock().Step() % kMarketTickSteps == kMarketTickSteps - 1)
			     TickMarkets(registry);
	     }});
}

void LoadTradeDefinitions(Sim::DefinitionRegistry &definitions)
{
	RegisterTypes();
	definitions.Load<Good>("defs/goods", Serialise::FormatVersion{kTradeDefinitionVersion});
	definitions.Load<MarketDefinition>(
	    "defs/markets", Serialise::FormatVersion{kTradeDefinitionVersion});
}

std::vector<entt::entity> OpenMarkets(Sim::SimWorld &world, const Sim::DefinitionRegistry &definitions)
{
	std::vector<entt::entity> opened;
	entt::registry &registry = world.Registry();
	for (const std::string &name : definitions.Names<MarketDefinition>())
	{
		const MarketDefinition &definition = *definitions.Find<MarketDefinition>(name);
		Market market{
		    definition.station, Sim::SystemId(definition.system), entt::null, definition.feeBps, {}};
		for (const MarketListing &listing : definition.goods)
		{
			const Good *good = definitions.Find<Good>(listing.good);
			if (!good)
			{
				Core::Log(
				    Core::Severity::Warning, kTrading,
				    "The market '{}' lists '{}', which is not a good", name, listing.good);
				continue;
			}
			MarketGood held{
			    listing.good,
			    good->basePrice,
			    listing.stock,
			    listing.production,
			    listing.consumption,
			    std::max<std::int64_t>(listing.target, 1),
			    0,
			    0};
			Quote(held, market.feeBps);
			market.goods.push_back(std::move(held));
		}
		const entt::entity entity = registry.create();
		registry.emplace<Market>(entity, std::move(market));
		opened.push_back(entity);
	}
	return opened;
}

std::int64_t ScarcityPrice(const MarketGood &good)
{
	const std::int64_t stock = std::max<std::int64_t>(good.stock, 0);
	const std::int64_t price = good.basePrice * 2 * good.target / (good.target + stock);
	return std::max<std::int64_t>(std::max(price, good.basePrice / 4), 1);
}

void Quote(MarketGood &good, std::uint32_t feeBps)
{
	const std::int64_t price = ScarcityPrice(good);
	const auto fee = static_cast<std::int64_t>(feeBps);
	good.ask = (price * (10000 + fee) + 9999) / 10000;
	good.bid = std::max<std::int64_t>(price * (10000 - fee) / 10000, 1);
}

void TickMarkets(entt::registry &registry)
{
	for (const auto [entity, market] : registry.view<Market>().each())
		for (MarketGood &good : market.goods)
		{
			// A full warehouse makes no more; what a ship sold it past that stays.
			good.stock += std::clamp<std::int64_t>(good.target * 4 - good.stock, 0, good.production);
			good.stock = std::max<std::int64_t>(good.stock - good.consumption, 0);
			Quote(good, market.feeBps);
		}
}

void ApplyTrade(Sim::SimWorld &world, const Trade &trade)
{
	entt::registry &registry = world.Registry();
	if (trade.crates == 0)
		return Refuse(world, trade, "no crates were asked for");
	if (!registry.valid(trade.ship) || !registry.all_of<Hold, Purse>(trade.ship))
		return Refuse(world, trade, "there is no such ship");
	if (!registry.valid(trade.market) || !registry.all_of<Market>(trade.market))
		return Refuse(world, trade, "there is no such market");
	Market &market = registry.get<Market>(trade.market);
	const auto *docked = registry.try_get<Docked>(trade.ship);
	const auto *port =
	    docked && registry.valid(docked->port) ? registry.try_get<DockingPort>(docked->port) : nullptr;
	if (!port || market.stationEntity == entt::null || port->station != market.stationEntity)
		return Refuse(world, trade, std::format("the ship is not docked at {}", market.station));
	MarketGood *good = FindGood(market, trade.good);
	if (!good)
		return Refuse(world, trade, std::format("{} does not deal in it", market.station));

	Hold &hold = registry.get<Hold>(trade.ship);
	Purse &purse = registry.get<Purse>(trade.ship);
	auto held = std::ranges::find(hold.cargo, trade.good, &Cargo::good);
	std::int64_t price = 0;
	std::int64_t cost = 0;
	if (trade.crates > 0)
	{
		price = good->ask;
		if (good->stock < trade.crates)
			return Refuse(world, trade, std::format("{} has {} crates", market.station, good->stock));
		if (hold.capacity - hold.Used() < trade.crates)
			return Refuse(
			    world, trade,
			    std::format("the hold has room for {} crates", hold.capacity - hold.Used()));
		if (!Cost(trade.crates, price, cost) || cost > purse.credits)
			return Refuse(
			    world, trade, std::format("{} crates cost more than the ship has", trade.crates));
		purse.credits -= cost;
		good->stock -= trade.crates;
		if (held == hold.cargo.end())
			hold.cargo.push_back({trade.good, trade.crates});
		else
			held->crates += trade.crates;
	}
	else
	{
		const std::int64_t crates = -trade.crates;
		price = good->bid;
		if (held == hold.cargo.end() || held->crates < crates)
			return Refuse(
			    world, trade,
			    std::format("the hold has {} crates", held == hold.cargo.end() ? 0 : held->crates));
		if (!Cost(crates, price, cost))
			return Refuse(world, trade, "the sale is worth more than a purse holds");
		purse.credits += cost;
		good->stock += crates;
		held->crates -= crates;
		if (held->crates == 0)
			hold.cargo.erase(held);
	}
	Quote(*good, market.feeBps);
	world.Events().Emit(
	    Traded{trade.ship, trade.market, trade.good, trade.crates, price, purse.credits});
}
} // namespace Jettison::App
