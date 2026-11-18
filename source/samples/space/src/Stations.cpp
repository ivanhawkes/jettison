#include "Stations.h"


#include <algorithm>
#include <array>
#include <iterator>
#include <string>
#include <utility>

#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Log.h>
#include <jettison/dock/Definitions.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Gates.h>
#include <jettison/space/Types.h>

namespace Jettison::Sample
{
namespace
{
constexpr Core::Category kStations{"stations"};


} // namespace


entt::entity FindStation(const entt::registry &world, std::uint64_t system, std::string_view body)
{
	for (const auto [entity, station, rails, inSystem] :
	     world.view<const Dock::Station, const Physics::OnRails, const Sim::InFrame>().each())
		if (inSystem.frame == system && rails.body == body)
			return entity;
	return entt::null;
}


void LoadStationDefinitions(Sim::DefinitionRegistry &definitions)
{
	Dock::RegisterTypes();
	Space::RegisterTypes();
	definitions.Load<Dock::PadClassDefinition>(
	    Dock::kPadClassDirectory, Serialise::FormatVersion{Dock::kDefinitionVersion});
	definitions.Load<Dock::HullDefinition>(Dock::kHullDirectory, Dock::HullMigrations());
	definitions.Load<Dock::StationLayout>(
	    Dock::kStationDirectory, Serialise::FormatVersion{Dock::kDefinitionVersion});
	definitions.Load<Space::CruiseDrive>(kDriveDirectory, Serialise::FormatVersion{kDriveVersion});
	for (const std::string &name : definitions.Names<Space::CruiseDrive>())
		Space::CheckCruiseDrive(name, *definitions.Find<Space::CruiseDrive>(name));
}


Drives ReadDrives(const Sim::DefinitionRegistry &definitions)
{
	Drives drives;
	for (const std::string &name : definitions.Names<Space::CruiseDrive>())
		drives.emplace(name, *definitions.Find<Space::CruiseDrive>(name));
	return drives;
}


void MakeShip(
    entt::registry &world, const Drives &drives, entt::entity ship, std::string_view hull, float mass,
    bool continuous)
{
	Physics::Body body{Physics::BodyKind::Dynamic, Dock::Docks::HullShape(hull), mass};
	body.friction = 0.4f;
	body.restitution = 0.2f;
	body.continuous = continuous;
	world.emplace_or_replace<Physics::Body>(ship, body);
	world.emplace_or_replace<Physics::Velocity>(ship);
	world.emplace_or_replace<Physics::Handling>(ship);
	world.emplace_or_replace<Physics::FlightControls>(ship);
	world.emplace_or_replace<Dock::Hull>(ship, std::string{hull});
	const auto drive = drives.find(hull);
	world.emplace_or_replace<Space::CruiseDrive>(
	    ship, drive == drives.end() ? Space::CruiseDrive{} : drive->second);
}


std::vector<entt::entity> OpenStations(
    Sim::SimWorld &world, Dock::Docks &docks, std::span<const Celestial::StarSystem> systems,
    const std::function<void(entt::entity station, std::string_view body)> &opened)
{
	std::vector<entt::entity> built;
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
			const std::uint64_t id = Sim::FrameId(system.name);
			const entt::entity station = docks.MakeStation(world, id, body.name, body.layout);
			built.push_back(station);
			if (opened)
				opened(station, body.name);
		}
	return built;
}


std::vector<entt::entity> OpenGates(
    Sim::SimWorld &world, const Space::Gates &gates, std::span<const Celestial::StarSystem> systems)
{
	std::vector<entt::entity> opened;
	for (const Celestial::StarSystem &system : systems)
	{
		const std::uint64_t id = Sim::FrameId(system.name);
		const auto made = [&](const Celestial::Body &body)
		{
			return body.kind == Celestial::BodyKind::JumpPoint &&
			       Space::Gates::FindGate(world.Registry(), id, body.name) != entt::null;
		};
		if (!gates.HasStarSystem(id) || std::ranges::any_of(system.bodies, made))
			continue;
		std::ranges::copy(gates.MakeGates(world, id), std::back_inserter(opened));
	}
	return opened;
}


entt::entity FreePad(
    const entt::registry &registry, const Dock::Docks &docks, entt::entity station,
    const Dock::HullDefinition &hull)
{
	const auto *built = registry.try_get<Dock::Station>(station);
	if (!built)
		return entt::null;
	// The station's pads are in their numbers' order.
	for (const entt::entity entity : built->pads)
		if (const Dock::Pad &pad = registry.get<Dock::Pad>(entity);
		    pad.state == Dock::PadState::Free && !pad.display && docks.Fits(hull, pad.padClass))
			return entity;
	return entt::null;
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


} // namespace Jettison::Sample
