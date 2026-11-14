#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

namespace Jettison::Dock
{
class Docks;
struct HullDefinition;
} // namespace Jettison::Dock

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
struct Snapshot;
} // namespace Jettison::Sim

namespace Jettison::App
{
// The sample game's fleet (docs/sample-game.md; docs/roadmap.md, "Decided": a player has a fleet):
// every ship a player commands (Sim::ControlledBy) is theirs, bought at a station with the credits of
// their purse, and each trades by the trader's tree (Traders.h) but the one they are at the helm of
// (Sim::Helm), which they fly. A ship of the fleet is fitted as a trader is, so letting go of its
// helm hands it to its behaviour, which plans its run from wherever it is.

// A hull a station sells, as data: defs/shipyard/<hull>.yaml, named by the hull it is
// (Dock::HullDefinition), so a mod sells another by adding a file. Every station sells every hull
// it has a pad for. Registered with Reflect as "Shell.ShipForSale", every field Saved; files at
// version 1.
struct ShipForSale
{
	// Credits.
	std::int64_t price{0};
	// Kilograms, empty.
	float mass{0.0f};
	// Crates.
	std::int64_t hold{0};
};

inline constexpr std::string_view kShipyardDirectory = "defs/shipyard";
inline constexpr std::uint32_t kShipyardVersion = 1;

// The command: the sender buys a ship of the hull at the station, for their owner's purse, and is
// at its helm from then on. Refused, with a BuyRefused, for a station that is not one the docks
// built, a hull not for sale (none in the shipyard, or none the docks know), a sender who owns
// nothing (no Owner, as no one in particular has none), one with no ship of their fleet docked at
// the station, a station with no free pad the hull fits, or a purse short of the price. The ship is
// made parked on the lowest numbered free pad the hull fits, commanded by the sender, its hold
// empty, fitted to trade; the price is drawn; and the sender takes its helm (Ai::TakeHelm), which
// lets go of the one they were at.
struct BuyShip
{
	Sim::Controller sender;
	entt::entity station{entt::null};
	std::string hull;
};

// Raised as a ship is bought, with the owner's credits after, and as a purchase is refused, saying
// why (TradeReason).
struct ShipBought
{
	Sim::Controller buyer;
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	std::string hull;
	std::int64_t price{0};
	std::int64_t credits{0};
};
struct BuyRefused
{
	Sim::Controller sender;
	entt::entity station{entt::null};
	std::string hull;
	Sim::Reason reason;
};

// The shipyard (defs/shipyard) into the registry. Throws as the registry's Load does, naming the
// file.
void LoadShipyard(Sim::DefinitionRegistry &definitions);

// Registers the types with Reflect and the BuyShip command, which sells from the shipyard's
// definitions as they are now; the world keeps the docks and its copy of them for as long as it
// lives.
void RegisterFleet(
    Sim::SimWorld &world, Dock::Docks &docks, const Sim::DefinitionRegistry &definitions);

// The station's lowest numbered free pad the hull fits, not a display one, or none.
entt::entity FreePad(
    const entt::registry &registry, entt::entity station, const Dock::HullDefinition &hull);

// Fits the ship to trade for its owner when no one is at its helm: steered, with a Trader and the
// trader's tree. What it already has is kept.
void FitForTrade(entt::registry &registry, entt::entity ship);

// The ships the controller commands, in entity order, as the snapshot shows them.
std::vector<entt::entity> FleetOf(const Sim::Snapshot &snapshot, const Sim::Controller &controller);
// The ship the controller is at the helm of, or none.
entt::entity HelmOf(const Sim::Snapshot &snapshot, const Sim::Controller &controller);

// A world saved before the helm (13d) has the player's one ship flown by no one, and not fitted to
// trade: the ship is given to the controller's helm, unless they are at one already, and fitted to
// trade once they leave it. A ship already fitted is of a later save, its helm left on purpose, and
// is left to its behaviour. Returns whether it was given.
bool GiveOldShipItsHelm(entt::registry &registry, entt::entity ship, const Sim::Controller &controller);
} // namespace Jettison::App
