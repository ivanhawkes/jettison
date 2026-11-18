#include "Repair.h"

#include <algorithm>
#include <string>

#include <jettison/dock/Docks.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/SimWorld.h>

#include "Trading.h"

namespace Jettison::Sample
{
namespace
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<Damage>("Shell.Damage").Field<&Damage::points>("points", Attribute::Saved);
		RegisterType<Repair>("Shell.Repair")
		    .Field<&Repair::sender>("sender", Attribute::Saved)
		    .Field<&Repair::ship>("ship", Attribute::Saved);
		RegisterType<Repaired>("Shell.Repaired")
		    .Field<&Repaired::ship>("ship", Attribute::Saved)
		    .Field<&Repaired::points>("points", Attribute::Saved)
		    .Field<&Repaired::cost>("cost", Attribute::Saved)
		    .Field<&Repaired::credits>("credits", Attribute::Saved);
		RegisterType<RepairRefused>("Shell.RepairRefused")
		    .Field<&RepairRefused::ship>("ship", Attribute::Saved)
		    .Field<&RepairRefused::reason>("reason", Attribute::Saved);
		return true;
	}();
	(void)registered;
}


void ApplyRepair(Sim::SimWorld &world, const Dock::Docks &docks, const Repair &repair)
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](TradeReason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{ world.Events().Emit(RepairRefused{repair.ship, Sim::MakeReason(reason, std::move(arguments))}); };
	if (!registry.valid(repair.ship) || !registry.all_of<Dock::Hull>(repair.ship))
		return refuse(TradeReason::NoShip);
	if (!Sim::MayCommand(registry, repair.ship, repair.sender))
		return refuse(TradeReason::NotOwner);
	Purse *pPurse = PurseOf(registry, repair.ship);
	if (!pPurse)
		return refuse(TradeReason::NoOwner);
	const auto *damage = registry.try_get<Damage>(repair.ship);
	if (!damage || damage->points <= 0)
		return refuse(TradeReason::NotDamaged);
	const auto *docking = registry.try_get<Dock::Docking>(repair.ship);
	if (!docking || docking->stage != Dock::Stage::Docked ||
	    !docks.Offers(registry, docking->pad, kDryDock))
		return refuse(TradeReason::NotInDryDock);
	// Points are metres a second summed over a ship's contacts, far from where the product overflows;
	// a save that says otherwise is refused as too dear rather than wrapped.
	const std::int64_t points = damage->points;
	if (points > pPurse->credits / kRepairPrice)
		return refuse(
		    TradeReason::ShortOfRepair,
		    {Sim::ReasonArgument::Integer(std::min(points, INT64_MAX / kRepairPrice) * kRepairPrice)});
	const std::int64_t cost = points * kRepairPrice;
	pPurse->credits -= cost;
	registry.remove<Damage>(repair.ship);
	world.Events().Emit(Repaired{repair.ship, points, cost, pPurse->credits});
}
} // namespace


void TakeDamage(entt::registry &registry, std::span<const Physics::ContactEvent> contacts)
{
	for (const Physics::ContactEvent &contact : contacts)
	{
		// A speed that is not a number compares false, and damages nothing.
		if (contact.phase != Physics::ContactPhase::Began || contact.sensor ||
		    !(contact.speed >= kDamagingSpeed))
			continue;
		// Bounded before it is an integer: physics holds a body under 100 km/s.
		const auto points = static_cast<std::int64_t>(std::min(contact.speed, 1.0e6f));
		for (const entt::entity body : {contact.first, contact.second})
			if (registry.valid(body) && registry.all_of<Dock::Hull>(body))
				registry.get_or_emplace<Damage>(body).points += points;
	}
}


void RegisterRepair(
    Sim::SimWorld &world, const Physics::PhysicsWorld &physics, const Dock::Docks &docks)
{
	RegisterTypes();
	world.RegisterComponent<Damage>();
	world.ShowInSnapshots<Damage>();
	world.RegisterCommand<Repair>([&docks](Sim::SimWorld &target, const Repair &repair)
	                              { ApplyRepair(target, docks, repair); });
	world.RegisterSystem(
	    {std::string{kDamageSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Dock::Hull>().Writing<Damage>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [&physics](entt::registry &registry, double) { TakeDamage(registry, physics.Contacts()); }});
}
} // namespace Jettison::Sample
