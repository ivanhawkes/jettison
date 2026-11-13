#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Sim
{
class DefinitionRegistry;
struct Snapshot;
} // namespace Jettison::Sim
namespace Jettison::Ui
{
class StringTable;
}

namespace Jettison::App
{
// The market screen's data (assets/ui/market.rml), which the UI's "market" model is bound to as
// "screen": presentation's copy, made from a snapshot's markets, hold and purse (Trading.h) and the
// goods' definitions, never from the world. Registered with Reflect, for the data model to read.
struct MarketRow
{
	std::string good;
	std::string name;
	std::string crate;
	// The good's galactic mean (GalacticPrices), or its base price in a world that has none.
	std::int64_t mean{0};
	std::int64_t stock{0};
	std::int64_t bid{0};
	std::int64_t ask{0};
	std::int64_t held{0};
	// The best bid among the other markets, as a line: "Corva Refinery pays 81,200".
	std::string elsewhere;
	bool canBuy{false};
	bool canSell{false};

	bool operator==(const MarketRow &) const = default;
};

struct MarketScreen
{
	std::string station;
	std::string system;
	bool docked{false};
	std::string status;
	std::int64_t credits{0};
	std::int64_t used{0};
	std::int64_t capacity{0};
	// Crates a click trades.
	std::int64_t lot{1};
	std::vector<MarketRow> goods;
	// The last trade's answer.
	std::string message;

	bool operator==(const MarketScreen &) const = default;
};

void RegisterMarketScreenTypes();

// What the screen shows of a market, for the ship, from a snapshot, in the string table's language: a
// good's name and crate are the table's (goods.<definition>.name and .crate), or where it has none
// the definition's. A market or ship the snapshot does not have leaves its part empty: a screen of no
// market says so in its status.
struct MarketView
{
	entt::entity market{entt::null};
	entt::entity ship{entt::null};
	std::string system;
	bool docked{false};
	std::int64_t lot{1};
	std::string message;
};
MarketScreen BuildMarketScreen(
    const Sim::Snapshot &snapshot, const Sim::DefinitionRegistry &definitions,
    const Ui::StringTable &strings, const MarketView &view);

// A good's name as the screens show it: the table's, or its definition's, or its own.
std::string GoodName(
    const Sim::DefinitionRegistry &definitions, const Ui::StringTable &strings, std::string_view good);

// A count of credits as the log writes one: 1,250,000. The screens write theirs in the player's
// language (Ui::StringTable::Number).
std::string FormatCredits(std::int64_t credits);
} // namespace Jettison::App
