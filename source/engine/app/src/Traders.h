#pragma once

#include <entt/entt.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/sim/Controller.h>

namespace Jettison::Ai
{
class Behaviours;
class LaneTravel;
struct DecisionDefinition;
} // namespace Jettison::Ai

namespace Jettison::Celestial
{
struct StarSystem;
}

namespace Jettison::Dock
{
class Docks;
}

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Script
{
class ScriptHost;
}

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::App
{
// The sample game's traders (docs/sample-game.md), the plan's other ships: each chooses a run by
// utility (defs/decisions/trade.yaml: which good, bought at which market and sold at which), and
// flies it by its behaviour tree (defs/behaviours/trader.yaml): off its pad, along the route across
// the lanes, through the gates, onto the pad the station gives it or into the queue for one, the
// trade, and the wait while the buggies carry its crates. The trees and the choice are the engine's
// (Ai::Behaviours, Ai::Choose); what a trader weighs and does at each step of its round is the
// game's, and this is the smallest that trades.

// A trader's run, and how far it has got. Simulation state, registered with Reflect as
// "Shell.Trader", every field Saved, and shown in snapshots.
struct Trader
{
	// The run chosen: the good, the market it is bought at (none to sell what the hold has) and the
	// one it is sold at; nothing between runs.
	std::string good;
	entt::entity buyAt{entt::null};
	entt::entity sellAt{entt::null};
	// The market it is making for, and the jump points still to go through on the way, in order.
	entt::entity bound{entt::null};
	std::vector<std::string> jumps;
	// What the run's purchase cost, and the runs sold and what they made over their costs.
	std::int64_t paid{0};
	std::uint32_t runs{0};
	std::int64_t earned{0};

	bool operator==(const Trader &) const = default;
};

inline constexpr std::string_view kTraderTree = "trader";
inline constexpr std::string_view kTradeDecision = "trade";

// Registers the types with Reflect, and Trader with the world, shown in snapshots; and defines the
// trader's tasks with the behaviours, which read the physics world, the lanes' galaxy and the star
// systems (the galaxy's, in its order) for where its gates and stations are, and weigh a run by
// the decision, whose inputs are a trader's (kTraderInputs) or, where scripts are given, a script's
// (Script::ScriptHost::WithScripts). The world keeps the tasks for as long as it lives, and the
// scripts outlive its last step.
void RegisterTraders(
    Sim::SimWorld &world, Ai::Behaviours &behaviours, const Physics::PhysicsWorld &physics,
    const Ai::LaneTravel &lanes, std::vector<Celestial::StarSystem> systems,
    const Ai::DecisionDefinition &decision, Script::ScriptHost *pScripts = nullptr);

// What a trader works out for an option of its decision.
inline constexpr std::array<std::string_view, 3> kTraderInputs{"margin", "rivals", "rate"};
// The first input the decision asks for that neither a trader nor one of the scripts answers, or none.
std::optional<std::string> UnansweredInput(
    const Ai::DecisionDefinition &decision, const Script::ScriptHost *pScripts);
// The decisions (defs/decisions) and the trees (defs/behaviours) into the registry. Throws as the
// registry's Load does, naming the file.
void LoadTraderDefinitions(Sim::DefinitionRegistry &definitions);

// Who sends a trader's commands, and commands it: the simulation's own agent, by the trader's
// number.
Sim::Controller TraderController(std::uint32_t number);

// Parks the sample's traders on free pads of the stations open, each a ship of its hull with a
// hold, a purse, a Trader and the trader's tree, commanded by the AI: transports on four of
// Highport's L pads and one of Corva's M pads, and shuttles on an L pad and Corva's S pad, which
// leaves Corva one pad free for the traders that come to it, so they queue. A station not open, or a
// pad already taken, is left out, saying so. Returns the traders, in the order made.
std::vector<entt::entity> OpenTraders(Sim::SimWorld &world, Dock::Docks &docks);
} // namespace Jettison::App
