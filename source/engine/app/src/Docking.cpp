#include "Docking.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/SimWorld.h>

namespace Jettison::App
{
namespace
{
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterType<DockingPort>("Shell.DockingPort")
		    .Field<&DockingPort::station>("station", Attribute::Saved)
		    .Field<&DockingPort::maxSpeed>("maxSpeed", Attribute::Saved);
		RegisterType<Docked>("Shell.Docked").Field<&Docked::port>("port", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

// How fast the ship moves relative to the station, both in the one frame: none for a station with
// no velocity, which is still in it.
float RelativeSpeed(const entt::registry &registry, entt::entity ship, entt::entity station)
{
	const auto *velocity = registry.try_get<Physics::Velocity>(ship);
	if (!velocity)
		return 0.0f;
	const auto *reference =
	    registry.valid(station) ? registry.try_get<Physics::Velocity>(station) : nullptr;
	return glm::length(velocity->linear - (reference ? reference->linear : glm::vec3{0.0f}));
}
} // namespace

void RegisterDocking(Sim::SimWorld &world, const Physics::PhysicsWorld &physics)
{
	RegisterTypes();
	world.RegisterComponent<DockingPort>();
	world.RegisterComponent<Docked>();
	world.RegisterSystem(
	    {std::string{kDockingSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<DockingPort, Physics::FlightControls, Physics::Velocity>().Writing<Docked>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [&physics, &world](entt::registry &registry, double)
	     { StepDocking(registry, physics, world.Events()); }});
}

void StepDocking(
    entt::registry &registry, const Physics::PhysicsWorld &physics, Sim::EventQueue &events)
{
	// What leaves, then what arrives, each in entity order, so a ship that leaves one port for
	// another in a step undocks before it docks.
	std::vector<std::pair<entt::entity, entt::entity>> leaving;
	for (const auto [ship, docked] : registry.view<const Docked>().each())
	{
		const auto *port =
		    registry.valid(docked.port) ? registry.try_get<DockingPort>(docked.port) : nullptr;
		if (!port || !std::ranges::binary_search(physics.Touching(docked.port), ship))
			leaving.emplace_back(ship, port ? port->station : entt::entity{entt::null});
	}
	std::ranges::sort(leaving);
	for (const auto &[ship, station] : leaving)
	{
		registry.remove<Docked>(ship);
		events.Emit(ShipUndocked{ship, station});
	}

	std::vector<std::pair<entt::entity, entt::entity>> arriving;
	for (const auto [port, dock] : registry.view<const DockingPort>().each())
		for (const entt::entity ship : physics.Touching(port))
			if (registry.all_of<Physics::FlightControls>(ship) && !registry.all_of<Docked>(ship) &&
			    RelativeSpeed(registry, ship, dock.station) <= dock.maxSpeed)
				arriving.emplace_back(ship, port);
	std::ranges::sort(arriving);
	for (const auto &[ship, port] : arriving)
	{
		// A ship in two ports at once docks at the first.
		if (registry.all_of<Docked>(ship))
			continue;
		registry.emplace<Docked>(ship, port);
		events.Emit(ShipDocked{ship, registry.get<DockingPort>(port).station});
	}
}
} // namespace Jettison::App
