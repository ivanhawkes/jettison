#include "Fleet.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Stations.h"
#include "Traders.h"
#include "Trading.h"

namespace Jettison::App
{
namespace
{
constexpr std::string_view kHullDefinition = "Dock.HullDefinition";

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<ShipForSale>("Shell.ShipForSale")
		    .Field<&ShipForSale::price>("price", Attribute::Saved)
		    .Field<&ShipForSale::mass>("mass", Attribute::Saved)
		    .Field<&ShipForSale::hold>("hold", Attribute::Saved);
		RegisterType<BuyShip>("Shell.BuyShip")
		    .Field<&BuyShip::sender>("sender", Attribute::Saved)
		    .Field<&BuyShip::station>("station", Attribute::Saved)
		    .Field<&BuyShip::hull>("hull", Attribute::Saved);
		RegisterType<ShipBought>("Shell.ShipBought", Attribute::Script)
		    .Field<&ShipBought::buyer>("buyer")
		    .Field<&ShipBought::ship>("ship")
		    .Field<&ShipBought::station>("station")
		    .Field<&ShipBought::hull>("hull")
		    .Field<&ShipBought::price>("price")
		    .Field<&ShipBought::credits>("credits");
		RegisterType<BuyRefused>("Shell.BuyRefused")
		    .Field<&BuyRefused::sender>("sender")
		    .Field<&BuyRefused::station>("station")
		    .Field<&BuyRefused::hull>("hull")
		    .Field<&BuyRefused::reason>("reason");
		return true;
	}();
	(void)registered;
}

// Whether one of the controller's ships is clamped to a pad of the station.
bool FleetDockedAt(
    const entt::registry &registry, const Sim::Controller &controller, entt::entity station)
{
	for (const auto [ship, controlled] : registry.view<const Sim::ControlledBy>().each())
		if (controlled.controller == controller && Dock::IsDocked(registry, ship, station))
			return true;
	return false;
}

void Buy(
    Sim::SimWorld &world, Dock::Docks &docks,
    const std::map<std::string, ShipForSale, std::less<>> &yard, const BuyShip &command)
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](TradeReason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{
		world.Events().Emit(
		    BuyRefused{
		        command.sender, command.station, command.hull,
		        Sim::MakeReason(reason, std::move(arguments))});
	};
	if (!registry.valid(command.station) || !registry.all_of<Dock::Station>(command.station))
		return refuse(TradeReason::NoStation);
	const auto sold = yard.find(command.hull);
	const Dock::HullDefinition *hull = docks.FindHull(command.hull);
	if (sold == yard.end() || !hull)
		return refuse(
		    TradeReason::NotForSale,
		    {Sim::ReasonArgument::Identifier(command.hull, std::string{kHullDefinition})});
	const entt::entity owner = FindOwner(registry, command.sender);
	Purse *purse = owner == entt::null ? nullptr : registry.try_get<Purse>(owner);
	if (!purse)
		return refuse(TradeReason::NoOwner);
	const Sim::ReasonArgument at = Sim::ReasonArgument::Entity(command.station);
	if (!FleetDockedAt(registry, command.sender, command.station))
		return refuse(TradeReason::NoneDocked, {at});
	const entt::entity pad = FreePad(registry, command.station, *hull);
	if (pad == entt::null)
		return refuse(
		    TradeReason::NoPadFree,
		    {at, Sim::ReasonArgument::Identifier(command.hull, std::string{kHullDefinition})});
	const ShipForSale &sale = sold->second;
	if (purse->credits < sale.price)
		return refuse(TradeReason::ShortOfPrice, {Sim::ReasonArgument::Integer(sale.price)});

	purse->credits -= sale.price;
	const std::int64_t credits = purse->credits;
	// Made where the station is, then parked: as the traders are (OpenTraders).
	const entt::entity ship = registry.create();
	registry.emplace<Sim::Transform>(ship, registry.get<Sim::Transform>(command.station));
	registry.emplace<Sim::InSystem>(ship, registry.get<Sim::InSystem>(command.station).system);
	registry.emplace<Physics::LocalFrame>(ship, registry.get<Physics::OnRails>(command.station).body);
	MakeShip(registry, docks, ship, command.hull, sale.mass, true);
	registry.replace<Physics::FlightControls>(
	    ship, Physics::FlightControls{{}, {}, true, command.station});
	registry.emplace<Sim::ControlledBy>(ship, command.sender);
	registry.emplace<Hold>(ship, Hold{sale.hold, {}});
	FitForTrade(registry, ship);
	docks.Park(world, ship, pad);
	world.Events().Emit(
	    ShipBought{command.sender, ship, command.station, command.hull, sale.price, credits});
	world.Apply(Ai::TakeHelm{command.sender, ship});
}
} // namespace

void LoadShipyard(Sim::DefinitionRegistry &definitions)
{
	RegisterTypes();
	definitions.Load<ShipForSale>(kShipyardDirectory, Serialise::FormatVersion{kShipyardVersion});
}

void RegisterFleet(Sim::SimWorld &world, Dock::Docks &docks, const Sim::DefinitionRegistry &definitions)
{
	RegisterTypes();
	// Sold as the definitions are at start-up, as a tree's tasks are defined: the game's, not state.
	auto yard = std::make_shared<std::map<std::string, ShipForSale, std::less<>>>();
	for (const std::string &name : definitions.Names<ShipForSale>())
		yard->emplace(name, *definitions.Find<ShipForSale>(name));
	world.RegisterCommand<BuyShip>([&docks, yard](Sim::SimWorld &target, const BuyShip &command)
	                               { Buy(target, docks, *yard, command); });
}

entt::entity FreePad(
    const entt::registry &registry, entt::entity station, const Dock::HullDefinition &hull)
{
	const auto *built = registry.try_get<Dock::Station>(station);
	if (!built)
		return entt::null;
	// The station's pads are in their numbers' order.
	for (const entt::entity entity : built->pads)
		if (const Dock::Pad &pad = registry.get<Dock::Pad>(entity);
		    pad.state == Dock::PadState::Free && !pad.display && pad.padClass >= hull.pad)
			return entity;
	return entt::null;
}

void FitForTrade(entt::registry &registry, entt::entity ship)
{
	if (!registry.all_of<Ai::Steered>(ship))
		registry.emplace<Ai::Steered>(ship);
	if (!registry.all_of<Trader>(ship))
		registry.emplace<Trader>(ship);
	if (!registry.all_of<Ai::Behaviour>(ship))
		registry.emplace<Ai::Behaviour>(ship, Ai::Behaviour{std::string{kTraderTree}, {}, {}});
}

std::vector<entt::entity> FleetOf(const Sim::Snapshot &snapshot, const Sim::Controller &controller)
{
	std::vector<entt::entity> fleet;
	for (const auto &[entity, controlled] : snapshot.Components<Sim::ControlledBy>())
		if (controlled.controller == controller)
			fleet.push_back(entity);
	std::ranges::sort(fleet, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	return fleet;
}

entt::entity HelmOf(const Sim::Snapshot &snapshot, const Sim::Controller &controller)
{
	for (const auto &[entity, helm] : snapshot.Components<Sim::Helm>())
		if (helm.controller == controller)
			return entity;
	return entt::null;
}

bool GiveOldShipItsHelm(entt::registry &registry, entt::entity ship, const Sim::Controller &controller)
{
	if (!registry.valid(ship) || registry.all_of<Sim::Helm>(ship) ||
	    !Sim::MayCommand(registry, ship, controller))
		return false;
	for (const auto [entity, helm] : registry.view<const Sim::Helm>().each())
		if (helm.controller == controller)
			return false;
	registry.emplace<Sim::Helm>(ship, controller);
	FitForTrade(registry, ship);
	return true;
}
} // namespace Jettison::App
