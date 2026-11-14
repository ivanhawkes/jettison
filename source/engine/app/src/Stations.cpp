#include "Stations.h"


#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <utility>

#include <jettison/ai/Cruise.h>
#include <jettison/ai/Gates.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Log.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::App
{
namespace
{
constexpr Core::Category kStations{"stations"};

// The sample's parked ships: on two or three pads of each station, leaving at least two free.
struct Parked
{
	std::string_view station;
	std::uint32_t pad;
	std::string_view hull;
	float mass;
};
constexpr std::array<Parked, 6> kParked{{
    {"Ostra Highport", 2, "heavy-transport", 1.2e7f},
    {"Ostra Highport", 7, "heavy-transport", 1.2e7f},
    {"Ostra Highport", 11, "heavy-transport", 1.2e7f},
    {"Corva Refinery", 2, "transport", 1.5e6f},
    {"Corva Refinery", 3, "transport", 1.5e6f},
    {"Corva Refinery", 6, "fighter", 3.0e4f},
}};

} // namespace

entt::entity FindStation(const entt::registry &world, std::uint64_t system, std::string_view body)
{
	for (const auto [entity, station, rails, inSystem] :
	     world.view<const Dock::Station, const Physics::OnRails, const Sim::InSystem>().each())
		if (inSystem.system == system && rails.body == body)
			return entity;
	return entt::null;
}

void MakeShip(
    entt::registry &world, const Dock::Docks &docks, entt::entity ship, std::string_view hull,
    float mass, bool continuous)
{
	Physics::Body body{Physics::BodyKind::Dynamic, Dock::Docks::HullShape(hull), mass};
	body.friction = 0.4f;
	body.restitution = 0.2f;
	body.continuous = continuous;
	world.emplace_or_replace<Physics::Body>(ship, body);
	world.emplace_or_replace<Physics::Velocity>(ship);
	world.emplace_or_replace<Physics::Thrusters>(ship);
	world.emplace_or_replace<Physics::FlightControls>(ship);
	world.emplace_or_replace<Dock::Hull>(ship, std::string{hull});
	const Dock::HullDefinition *definition = docks.FindHull(hull);
	world.emplace_or_replace<Ai::CruiseDrive>(
	    ship, definition ? definition->cruise : Ai::CruiseDrive{});
}

std::vector<entt::entity> OpenStations(
    Sim::SimWorld &world, Dock::Docks &docks, std::span<const Celestial::StarSystem> systems)
{
	std::vector<entt::entity> opened;
	entt::registry &registry = world.Registry();
	for (const Celestial::StarSystem &system : systems)
		for (const Celestial::Body &body : system.bodies)
		{
			if (body.kind != Celestial::BodyKind::Station || body.layout.empty())
				continue;
			if (!docks.FindLayout(body.layout))
			{
				Core::Log(
				    Core::Severity::Warning, kStations,
				    "'{}' is built to '{}', which is not a station layout; it has no pads", body.name,
				    body.layout);
				continue;
			}
			const std::uint64_t id = Sim::SystemId(system.name);
			const entt::entity station = docks.MakeStation(world, id, body.name, body.layout);
			opened.push_back(station);
			for (const Parked &parked : kParked)
			{
				if (parked.station != body.name || !docks.FindHull(parked.hull))
					continue;
				const entt::entity ship = registry.create();
				registry.emplace<Sim::Transform>(ship, registry.get<Sim::Transform>(station));
				registry.emplace<Sim::InSystem>(ship, id);
				registry.emplace<Physics::LocalFrame>(ship, body.name);
				MakeShip(registry, docks, ship, parked.hull, parked.mass, false);
				docks.Park(world, ship, registry.get<Dock::Station>(station).pads.at(parked.pad - 1));
			}
		}
	return opened;
}

std::vector<entt::entity> OpenGates(
    Sim::SimWorld &world, const Ai::Gates &gates, std::span<const Celestial::StarSystem> systems)
{
	std::vector<entt::entity> opened;
	for (const Celestial::StarSystem &system : systems)
	{
		const std::uint64_t id = Sim::SystemId(system.name);
		const auto made = [&](const Celestial::Body &body)
		{
			return body.kind == Celestial::BodyKind::JumpPoint &&
			       Ai::Gates::FindGate(world.Registry(), id, body.name) != entt::null;
		};
		if (!gates.HasStarSystem(id) || std::ranges::any_of(system.bodies, made))
			continue;
		std::ranges::copy(gates.MakeGates(world, id), std::back_inserter(opened));
	}
	return opened;
}

} // namespace Jettison::App
