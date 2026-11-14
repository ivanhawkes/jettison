#include "Trading.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <jettison/ai/Gates.h>
#include <jettison/core/Log.h>
#include <jettison/dock/Docks.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>


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
		RegisterType<GalacticPrice>("Shell.GalacticPrice")
		    .Field<&GalacticPrice::good>("good", Attribute::Saved)
		    .Field<&GalacticPrice::mean>("mean", Attribute::Saved);
		RegisterType<GalacticPrices>("Shell.GalacticPrices")
		    .Field<&GalacticPrices::goods>("goods", Attribute::Saved);
		RegisterType<Owner>("Shell.Owner").Field<&Owner::controller>("controller", Attribute::Saved);
		RegisterType<Purse>("Shell.Purse").Field<&Purse::credits>("credits", Attribute::Saved);
		RegisterType<Trade>("Shell.Trade", Attribute::Script)
		    .Field<&Trade::sender>("sender", Attribute::Saved)
		    .Field<&Trade::ship>("ship", Attribute::Saved)
		    .Field<&Trade::market>("market", Attribute::Saved)
		    .Field<&Trade::good>("good", Attribute::Saved)
		    .Field<&Trade::crates>("crates", Attribute::Saved);
		// The events, by name, so data can map them to sounds (Audio::EventSound); not saved.
		RegisterType<Traded>("Shell.Traded", Attribute::Script)
		    .Field<&Traded::ship>("ship")
		    .Field<&Traded::market>("market")
		    .Field<&Traded::good>("good")
		    .Field<&Traded::crates>("crates")
		    .Field<&Traded::price>("price")
		    .Field<&Traded::credits>("credits");
		RegisterType<TradeRefused>("Shell.TradeRefused", Attribute::Script)
		    .Field<&TradeRefused::ship>("ship")
		    .Field<&TradeRefused::good>("good")
		    .Field<&TradeRefused::reason>("reason");
		RegisterEnum<TradeReason>("Shell.Reason")
		    .Value<TradeReason::NoCrates>("no_crates")
		    .Value<TradeReason::NoShip>("no_ship")
		    .Value<TradeReason::NotOwner>("not_owner")
		    .Value<TradeReason::NoOwner>("no_owner")
		    .Value<TradeReason::NoMarket>("no_market")
		    .Value<TradeReason::NotDocked>("not_docked")
		    .Value<TradeReason::NotDealt>("not_dealt")
		    .Value<TradeReason::ShortOfStock>("short_of_stock")
		    .Value<TradeReason::NoRoom>("no_room")
		    .Value<TradeReason::TooDear>("too_dear")
		    .Value<TradeReason::HoldShort>("hold_short")
		    .Value<TradeReason::SaleTooLarge>("sale_too_large")
		    .Value<TradeReason::ShortOfToll>("short_of_toll");
		return true;
	}();
	(void)registered;
}

void Refuse(
    Sim::SimWorld &world, const Trade &trade, TradeReason reason,
    std::vector<Sim::ReasonArgument> arguments = {})
{
	world.Events().Emit(
	    TradeRefused{trade.ship, trade.good, Sim::MakeReason(reason, std::move(arguments))});
}

Sim::ReasonArgument Crates(std::int64_t crates)
{
	return Sim::ReasonArgument::Integer(crates);
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

entt::entity MakeOwner(
    entt::registry &registry, const Sim::Controller &controller, std::int64_t credits)
{
	if (FindOwner(registry, controller) != entt::null)
		throw std::logic_error{"Trading: the controller already has an owner"};
	const entt::entity owner = registry.create();
	registry.emplace<Owner>(owner, controller);
	registry.emplace<Purse>(owner, Purse{credits});
	return owner;
}

entt::entity FindOwner(const entt::registry &registry, const Sim::Controller &controller)
{
	// One owner a controller (MakeOwner), so the view's order decides nothing.
	for (const auto [entity, owner] : registry.view<const Owner>().each())
		if (owner.controller == controller)
			return entity;
	return entt::null;
}

entt::entity OwnerOf(const entt::registry &registry, entt::entity ship)
{
	const auto *controlled = registry.valid(ship) ? registry.try_get<Sim::ControlledBy>(ship) : nullptr;
	return controlled ? FindOwner(registry, controlled->controller) : entt::null;
}

Purse *PurseOf(entt::registry &registry, entt::entity ship)
{
	const entt::entity owner = OwnerOf(registry, ship);
	return owner != entt::null ? registry.try_get<Purse>(owner) : nullptr;
}

const Purse *PurseOf(const entt::registry &registry, entt::entity ship)
{
	const entt::entity owner = OwnerOf(registry, ship);
	return owner != entt::null ? registry.try_get<Purse>(owner) : nullptr;
}

const Purse *PurseOf(const Sim::Snapshot &snapshot, entt::entity ship)
{
	const auto *controlled = snapshot.Get<Sim::ControlledBy>(ship);
	if (!controlled)
		return nullptr;
	for (const auto &[entity, owner] : snapshot.Components<Owner>())
		if (owner.controller == controlled->controller)
			return snapshot.Get<Purse>(entity);
	return nullptr;
}

const GalacticPrice *GalacticPrices::Find(std::string_view good) const
{
	const auto found = std::ranges::find(goods, good, &GalacticPrice::good);
	return found != goods.end() ? &*found : nullptr;
}

std::int64_t MoveMean(std::int64_t mean, std::int64_t price, std::int64_t crates)
{
	const std::int64_t weight = std::min(crates < 0 ? -crates : crates, kGalacticMemoryCrates);
	const std::int64_t total = kGalacticMemoryCrates + weight;
	// Rounded half away from zero, as both are whole credits and never negative.
	return (mean * kGalacticMemoryCrates + price * weight + total / 2) / total;
}

const GalacticPrices *FindGalacticPrices(const entt::registry &registry)
{
	const auto view = registry.view<const GalacticPrices>();
	return view.empty() ? nullptr : &view.get<const GalacticPrices>(view.front());
}

const GalacticPrices *FindGalacticPrices(const Sim::Snapshot &snapshot)
{
	const auto samples = snapshot.Components<GalacticPrices>();
	return samples.empty() ? nullptr : &samples.front().value;
}

std::size_t MovePursesToOwners(entt::registry &registry)
{
	std::vector<entt::entity> ships;
	for (const auto [entity, purse, controlled] :
	     registry.view<const Purse, const Sim::ControlledBy>().each())
		if (!registry.all_of<Owner>(entity))
			ships.push_back(entity);
	std::ranges::sort(ships, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	for (const entt::entity ship : ships)
	{
		const Sim::Controller controller = registry.get<Sim::ControlledBy>(ship).controller;
		const std::int64_t credits = registry.get<Purse>(ship).credits;
		registry.remove<Purse>(ship);
		if (const entt::entity owner = FindOwner(registry, controller); owner != entt::null)
			registry.get_or_emplace<Purse>(owner).credits += credits;
		else
			MakeOwner(registry, controller, credits);
	}
	return ships.size();
}

void RegisterTrading(Sim::SimWorld &world)
{
	RegisterTypes();
	world.RegisterComponent<Market>();
	world.RegisterComponent<Hold>();
	world.RegisterComponent<Purse>();
	world.RegisterComponent<Owner>();
	world.RegisterComponent<GalacticPrices>();
	world.ShowInSnapshots<Market>();
	world.ShowInSnapshots<Hold>();
	world.ShowInSnapshots<Purse>();
	world.ShowInSnapshots<Owner>();
	world.ShowInSnapshots<GalacticPrices>();
	world.ShowInSnapshots<Sim::ControlledBy>();
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
	GalacticPrices prices;
	for (const std::string &name : definitions.Names<Good>())
		prices.goods.push_back({name, definitions.Find<Good>(name)->basePrice});
	registry.emplace<GalacticPrices>(registry.create(), std::move(prices));
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
		return Refuse(world, trade, TradeReason::NoCrates);
	if (!registry.valid(trade.ship) || !registry.all_of<Hold>(trade.ship))
		return Refuse(world, trade, TradeReason::NoShip);
	if (!Sim::MayCommand(registry, trade.ship, trade.sender))
		return Refuse(world, trade, TradeReason::NotOwner);
	Purse *pPurse = PurseOf(registry, trade.ship);
	if (!pPurse)
		return Refuse(world, trade, TradeReason::NoOwner);
	if (!registry.valid(trade.market) || !registry.all_of<Market>(trade.market))
		return Refuse(world, trade, TradeReason::NoMarket);
	Market &market = registry.get<Market>(trade.market);
	const Sim::ReasonArgument at = Sim::ReasonArgument::Entity(trade.market);
	if (market.stationEntity == entt::null ||
	    !Dock::IsDocked(registry, trade.ship, market.stationEntity))
		return Refuse(world, trade, TradeReason::NotDocked, {at});
	MarketGood *good = FindGood(market, trade.good);
	if (!good)
		return Refuse(
		    world, trade, TradeReason::NotDealt,
		    {at, Sim::ReasonArgument::Identifier(trade.good, "Shell.Good")});

	Hold &hold = registry.get<Hold>(trade.ship);
	Purse &purse = *pPurse;
	auto held = std::ranges::find(hold.cargo, trade.good, &Cargo::good);
	std::int64_t price = 0;
	std::int64_t cost = 0;
	if (trade.crates > 0)
	{
		price = good->ask;
		if (good->stock < trade.crates)
			return Refuse(world, trade, TradeReason::ShortOfStock, {at, Crates(good->stock)});
		if (hold.capacity - hold.Used() < trade.crates)
			return Refuse(world, trade, TradeReason::NoRoom, {Crates(hold.capacity - hold.Used())});
		if (!Cost(trade.crates, price, cost) || cost > purse.credits)
			return Refuse(world, trade, TradeReason::TooDear, {Crates(trade.crates)});
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
			    world, trade, TradeReason::HoldShort,
			    {Crates(held == hold.cargo.end() ? 0 : held->crates)});
		if (!Cost(crates, price, cost))
			return Refuse(world, trade, TradeReason::SaleTooLarge);
		purse.credits += cost;
		good->stock += crates;
		held->crates -= crates;
		if (held->crates == 0)
			hold.cargo.erase(held);
	}
	Quote(*good, market.feeBps);
	auto pricesView = registry.view<GalacticPrices>();
	const entt::entity galaxy = pricesView.empty() ? registry.create() : pricesView.front();
	auto &prices = registry.get_or_emplace<GalacticPrices>(galaxy);
	auto mean = std::ranges::find(prices.goods, trade.good, &GalacticPrice::good);
	if (mean == prices.goods.end())
		mean = prices.goods.insert(prices.goods.end(), {trade.good, good->basePrice});
	mean->mean = MoveMean(mean->mean, price, trade.crates);
	// Settled at once; the crates then cross the deck, and the ship stays clamped until they have.
	Dock::Carry(registry, trade.ship, trade.crates);
	world.Events().Emit(
	    Traded{trade.ship, trade.market, trade.good, trade.crates, price, purse.credits});
}

void ChargeTolls(Ai::Gates &gates)
{
	gates.SetTollRule(
	    {[](const entt::registry &registry, entt::entity ship,
	        std::int64_t toll) -> std::optional<Sim::Reason>
	     {
		     const Purse *purse = PurseOf(registry, ship);
		     if (!purse)
			     return Sim::MakeReason(TradeReason::NoOwner);
		     if (purse->credits < toll)
			     return Sim::MakeReason(TradeReason::ShortOfToll, {Sim::ReasonArgument::Integer(toll)});
		     return std::nullopt;
	     },
	     [](entt::registry &registry, entt::entity ship, std::int64_t toll)
	     { PurseOf(registry, ship)->credits -= toll; }});
}
} // namespace Jettison::App
