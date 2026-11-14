#include <jettison/ai/Lanes.h>

#include <jettison/ai/Cruise.h>
#include <jettison/ai/Gates.h>
#include <jettison/ai/Steering.h>

#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include "Transit.h"

namespace Jettison::Ai
{
LaneTravel &LaneTravel::Attach(Sim::SimWorld &world, const Physics::PhysicsWorld &physics)
{
	RegisterTypes();
	// Shared by the command and the system, which the world keeps for as long as it lives.
	std::shared_ptr<LaneTravel> lanes{new LaneTravel{physics}};
	world.RegisterComponent<OnLane>();
	world.RegisterCommand<EnterLane>([lanes](Sim::SimWorld &target, const EnterLane &command)
	                                 { lanes->Enter(target, command); });
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Physics::Body>()
	         .Writing<
	             OnLane, Sim::InSystem, Sim::Transform, Physics::LocalFrame, Physics::FlightControls>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [lanes, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	     { lanes->Arrive(world); }});
	return *lanes;
}

void LaneTravel::SetGalaxy(Celestial::Galaxy galaxy)
{
	m_galaxy = std::move(galaxy);
}

void LaneTravel::DefineStarSystem(Celestial::StarSystem system)
{
	const std::uint64_t id = Sim::SystemId(system.name);
	if (m_systems.contains(id))
		throw std::invalid_argument{
		    std::format("Ai: star system '{}' is already defined", system.name)};
	m_systems.emplace(id, std::move(system));
}

bool LaneTravel::HasStarSystem(std::uint64_t system) const
{
	return m_systems.contains(system);
}

void LaneTravel::Enter(Sim::SimWorld &world, const EnterLane &command) const
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](Reason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{
		world.Events().Emit(
		    LaneRefused{
		        command.entity, command.jumpPoint, Sim::MakeReason(reason, std::move(arguments))});
	};
	if (!registry.valid(command.entity))
		return refuse(Reason::NoSuchEntity);
	const auto *inSystem = registry.try_get<Sim::InSystem>(command.entity);
	if (!inSystem || inSystem->system == Sim::kNoSystem)
		return refuse(registry.all_of<OnLane>(command.entity) ? Reason::OnLane : Reason::InNoSystem);
	if (registry.all_of<Cruising>(command.entity))
		return refuse(Reason::AlreadyCruising);
	if (!m_galaxy)
		return refuse(Reason::NoGalaxy);
	const auto system = std::ranges::find(
	    m_galaxy->systems, inSystem->system,
	    [](const Celestial::GalaxySystem &entry) { return Sim::SystemId(entry.name); });
	if (system == m_galaxy->systems.end())
		return refuse(Reason::SystemNotInGalaxy);
	const std::optional<Celestial::Exit> exit = Celestial::ExitThrough(
	    *m_galaxy, static_cast<std::size_t>(system - m_galaxy->systems.begin()), command.jumpPoint);
	if (!exit)
		return refuse(
		    Reason::NoLane, {Sim::ReasonArgument::Identifier(system->name),
		                     Sim::ReasonArgument::Identifier(command.jumpPoint)});
	const std::uint64_t destination = Sim::SystemId(m_galaxy->systems[exit->destination].name);
	if (!m_systems.contains(destination))
		return refuse(
		    Reason::ArrivalUndefined,
		    {Sim::ReasonArgument::Identifier(m_galaxy->systems[exit->destination].name)});

	const OnLane lane{
	    inSystem->system, command.jumpPoint, destination, std::string{exit->arrival},
	    world.Clock().Step() + Detail::TransitSteps(exit->transitSeconds, world.FixedStep())};
	registry.emplace_or_replace<OnLane>(command.entity, lane);
	registry.replace<Sim::InSystem>(command.entity, Sim::kNoSystem);
	// Where it was going is in the system it left.
	registry.remove<Destination>(command.entity);
	world.Events().Emit(LaneEntered{command.entity, lane, exit->transitSeconds});
}

void LaneTravel::Arrive(Sim::SimWorld &world) const
{
	entt::registry &registry = world.Registry();
	// The step being run ends at the next count.
	const std::uint64_t ending = world.Clock().Step() + 1;
	std::vector<entt::entity> arriving;
	for (const auto [entity, lane] : registry.view<const OnLane>().each())
		if (lane.arrivalStep <= ending)
			arriving.push_back(entity);
	if (arriving.empty())
		return;
	std::ranges::sort(arriving, {}, [](entt::entity entity) { return entt::to_integral(entity); });

	// Every arrival is placed before any is made, so a step that cannot place one changes nothing.
	struct Arrival
	{
		Sim::Transform transform;
		std::string frame;
	};
	const double endsAt = world.Clock().Now() + world.FixedStep();
	std::vector<Arrival> places;
	for (const entt::entity entity : arriving)
	{
		const OnLane &lane = registry.get<const OnLane>(entity);
		const auto system = m_systems.find(lane.to);
		const int found = system == m_systems.end() ? -1 : system->second.Find(lane.arrival);
		if (found < 0 || system->second.bodies[static_cast<std::size_t>(found)].kind !=
		                     Celestial::BodyKind::JumpPoint)
			throw std::invalid_argument{std::format(
			    "Ai: entity {:#x} arrives at '{}' in a system {}", entt::to_integral(entity),
			    lane.arrival, system == m_systems.end() ? "not defined" : "without that jump point")};
		const Celestial::StarSystem &starSystem = system->second;
		const auto body = static_cast<std::size_t>(found);
		const int parent = starSystem.bodies[body].parentIndex;
		const bool atStation =
		    parent >= 0 &&
		    starSystem.bodies[static_cast<std::size_t>(parent)].kind == Celestial::BodyKind::Station;
		const glm::dquat facing = Celestial::JumpPointFacing(starSystem, body, endsAt);
		places.push_back(
		    {{Celestial::EvaluateBody(starSystem, body, endsAt).metres, facing},
		     atStation ? starSystem.bodies[static_cast<std::size_t>(parent)].name : lane.arrival});
	}

	// What is already in each system arrived in, where it is as the step ends; each arrival joins
	// them once it is placed.
	std::vector<std::uint64_t> destinations;
	destinations.reserve(arriving.size());
	for (const entt::entity entity : arriving)
		destinations.push_back(registry.get<const OnLane>(entity).to);
	std::vector<Detail::Occupant> occupants = Detail::Occupants(registry, m_physics, destinations);
	for (std::size_t index = 0; index < arriving.size(); ++index)
	{
		const auto *body = registry.try_get<const Physics::Body>(arriving[index]);
		if (!body)
			continue;
		const std::uint64_t to = registry.get<const OnLane>(arriving[index]).to;
		const double radius = m_physics.ShapeRadius(body->shape);
		Sim::Transform &place = places[index].transform;
		// Out ahead of the ring, clear of its sensor, so the gate does not see it pass through.
		place.position += place.orientation *
		                  glm::dvec3{0.0, 0.0, -(Gates::kRingDepth / 2.0 + radius + kArrivalClearance)};
		place.position = Detail::ClearPlace(occupants, to, place, radius, kArrivalClearance);
		occupants.push_back({to, place.position, radius});
	}

	for (std::size_t index = 0; index < arriving.size(); ++index)
	{
		const entt::entity entity = arriving[index];
		const OnLane lane = registry.get<const OnLane>(entity);
		registry.emplace_or_replace<Sim::Transform>(entity, places[index].transform);
		registry.emplace_or_replace<Sim::InSystem>(entity, lane.to);
		if (registry.all_of<Physics::Body>(entity))
		{
			registry.emplace_or_replace<Physics::LocalFrame>(entity, places[index].frame);
			registry.emplace_or_replace<Physics::Velocity>(entity);
		}
		if (auto *controls = registry.try_get<Physics::FlightControls>(entity))
			controls->reference = entt::null;
		registry.remove<OnLane>(entity);
		world.Events().Emit(LaneLeft{entity, lane.to, lane.arrival});
	}
}
} // namespace Jettison::Ai
