#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::App
{
// Trading as the shell plays it: the first slice of the sample game, whose plan is the Manifest
// project's (docs/sample-game.md). The engine's part is the definitions, the deterministic
// step, the save and the UI's binding; what a market is, is the game's, and this is the smallest one
// that trades.
//
// Goods are traded by the crate, one IBC cube (a cubic metre) of the good packed for shipping, priced
// in whole credits, a credit to the US dollar, at the plan's 2025 prices. A station's market holds a
// stock of each good it deals in, which it makes and uses at its own rates each market tick, and
// prices each by scarcity; it buys at its bid and sells at its ask, either side of the price by its
// listing fee. A ship trades at the market of the station it is docked at.

// A good, as data: defs/goods/<name>.yaml, loaded through Sim::DefinitionRegistry, so a mod adds or
// changes one. Registered with Reflect as "Shell.Good", every field Saved; files at version 1.
struct Good
{
	std::string displayName;
	// The plan's category and age: CategoryMetals, AgeCopper.
	std::string category;
	std::string age;
	// What a crate holds, as a screen says it: "7,616 kg of ingots".
	std::string crate;
	// Credits a crate, at the price the market's scarcity starts from.
	std::int64_t basePrice{0};
};

// One good a market deals in, as data.
struct MarketListing
{
	std::string good;
	// Crates at the start, made and used each market tick, and the stock at which the price is the
	// good's base: below it the price rises towards twice the base, above it falls towards a quarter.
	std::int64_t stock{0};
	std::int64_t production{0};
	std::int64_t consumption{0};
	std::int64_t target{1};
};

// A station's market, as data: defs/markets/<name>.yaml, naming the station's body and its system as
// the system files do. Registered as "Shell.MarketDefinition"; files at version 1.
struct MarketDefinition
{
	std::string station;
	std::string system;
	// In basis points of the price, on each side: 150 is a spread of 3% from bid to ask.
	std::uint32_t feeBps{150};
	std::vector<MarketListing> goods;
};

// What a market holds of one good, and its quote. Simulation state.
struct MarketGood
{
	std::string good;
	std::int64_t basePrice{0};
	std::int64_t stock{0};
	std::int64_t production{0};
	std::int64_t consumption{0};
	std::int64_t target{1};
	// What the market pays for a crate, and asks for one: refreshed whenever the stock moves.
	std::int64_t bid{0};
	std::int64_t ask{0};

	bool operator==(const MarketGood &) const = default;
};

// A station's market: an entity of its own, so a station not yet in the world (one in a system the
// ship has not been to) still trades on. Simulation state, registered with Reflect, every field
// Saved, and shown in snapshots, which the market screen reads.
struct Market
{
	std::string station;
	std::uint64_t system{0};
	// The station's simulation entity, a ship clamped to one of whose pads trades here (Dock); null
	// for a station that is not in the world, where no ship can dock.
	entt::entity stationEntity{entt::null};
	std::uint32_t feeBps{0};
	std::vector<MarketGood> goods;

	bool operator==(const Market &) const = default;
};

// A crate of one good in a hold.
struct Cargo
{
	std::string good;
	std::int64_t crates{0};

	bool operator==(const Cargo &) const = default;
};

// A ship's hold, in crates, and its credits. Simulation state, shown in snapshots.
struct Hold
{
	std::int64_t capacity{0};
	std::vector<Cargo> cargo;

	std::int64_t Used() const;
	std::int64_t Crates(std::string_view good) const;
	bool operator==(const Hold &) const = default;
};
struct Purse
{
	std::int64_t credits{0};

	bool operator==(const Purse &) const = default;
};

// The command: a ship buys crates of a good at a market (a positive count) or sells them (a
// negative one), at the market's quote as the step applies it. The crates and the price are settled
// at once, and the crates then cross the station's deck (Dock::Carry), the ship held on its pad
// until they have.
struct Trade
{
	entt::entity ship{entt::null};
	entt::entity market{entt::null};
	std::string good;
	std::int64_t crates{0};
};

// Raised as a trade fills, and as one is refused, saying why.
struct Traded
{
	entt::entity ship{entt::null};
	entt::entity market{entt::null};
	std::string good;
	std::int64_t crates{0};
	// Credits a crate, and the ship's credits after.
	std::int64_t price{0};
	std::int64_t credits{0};
};
struct TradeRefused
{
	entt::entity ship{entt::null};
	std::string good;
	std::string reason;
};

inline constexpr std::string_view kMarketSystemName = "shell.market";
// Steps between market ticks: ten seconds at the shell's sixty steps a second, so a market a ship
// left empty of a good is still short of it a few minutes later, when a ship could be back. The plan
// settles its markets every twenty of its ticks, which a game sets against its own calendar.
inline constexpr std::uint64_t kMarketTickSteps = 600;
// The format the goods' and the markets' files are at.
inline constexpr std::uint32_t kTradeDefinitionVersion = 1;

// Registers the types with Reflect, the components with the world (each shown in snapshots), the
// Trade command, and the system "shell.market", which ticks the markets every kMarketTickSteps steps.
void RegisterTrading(Sim::SimWorld &world);
// The goods (defs/goods) and markets (defs/markets) into the registry. Throws as the registry's Load
// does, naming the file.
void LoadTradeDefinitions(Sim::DefinitionRegistry &definitions);

// Makes a market entity from each market definition, its goods priced from their definitions. A
// listing of a good with no definition is left out, and logged. Returns the markets made, in the
// definitions' order.
std::vector<entt::entity> OpenMarkets(Sim::SimWorld &world, const Sim::DefinitionRegistry &definitions);

// The price a market quotes for a good at its stock: twice the base at none, the base at the target,
// a quarter of it at most at seven times the target and beyond. Whole credits, never below one.
std::int64_t ScarcityPrice(const MarketGood &good);
// The good's price, its bid and ask from it, by the fee: the ask rounded up and the bid down, so the
// market never pays out a fraction of a credit.
void Quote(MarketGood &good, std::uint32_t feeBps);

// One market tick: each market makes and uses its goods, at most four times the target held, and
// quotes again.
void TickMarkets(entt::registry &registry);
// Applies a trade, or refuses it with a TradeRefused: a ship or market that is not there, a ship not
// docked at the market's station, a good the market does not deal in, too few crates in the market
// or the hold, too few credits, too little room.
void ApplyTrade(Sim::SimWorld &world, const Trade &trade);
} // namespace Jettison::App
