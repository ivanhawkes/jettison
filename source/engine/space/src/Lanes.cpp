#include <jettison/space/Lanes.h>

#include <jettison/ai/Steering.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Gates.h>
#include <jettison/space/Types.h>

#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/transit/Path.h>
#include <jettison/transit/Portal.h>

#include <algorithm>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>


namespace Jettison::Space
{
using Ai::Destination;
using Ai::Steered;


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
	             OnLane, Sim::InFrame, Sim::Transform, Physics::LocalFrame, Physics::FlightControls>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [lanes, &world](entt::registry & /*registry*/, double /*fixedStep*/) { lanes->Arrive(world); },
	     {},
	     // Dock::Docks::kSystemName: space does not depend on dock, nor dock on space.
	     {"dock"}},
	    // Until the step an arrival ends at, nothing on a lane changes.
	    {.next = Jettison::Transit::NextArrival<OnLane>});
	return *lanes;
}


void LaneTravel::SetGalaxy(Celestial::Galaxy galaxy)
{
	m_galaxy = std::move(galaxy);
}


void LaneTravel::DefineStarSystem(Celestial::StarSystem system)
{
	const std::uint64_t id = Sim::FrameId(system.name);
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
	if (!Sim::MayCommand(registry, command.entity, command.sender))
		return refuse(Reason::NotController);
	const auto *inSystem = registry.try_get<Sim::InFrame>(command.entity);
	if (!inSystem || inSystem->frame == Sim::kNoFrame)
		return refuse(registry.all_of<OnLane>(command.entity) ? Reason::OnLane : Reason::InNoSystem);
	if (registry.all_of<Cruising>(command.entity))
		return refuse(Reason::AlreadyCruising);
	if (!m_galaxy)
		return refuse(Reason::NoGalaxy);
	const auto system = std::ranges::find(
	    m_galaxy->systems, inSystem->frame,
	    [](const Celestial::GalaxySystem &entry) { return Sim::FrameId(entry.name); });
	if (system == m_galaxy->systems.end())
		return refuse(Reason::SystemNotInGalaxy);
	const std::optional<Celestial::Exit> exit = Celestial::ExitThrough(
	    *m_galaxy, static_cast<std::size_t>(system - m_galaxy->systems.begin()), command.jumpPoint);
	if (!exit)
		return refuse(
		    Reason::NoLane, {Sim::ReasonArgument::Identifier(system->name),
		                     Sim::ReasonArgument::Identifier(command.jumpPoint)});
	const std::uint64_t destination = Sim::FrameId(m_galaxy->systems[exit->destination].name);
	if (!m_systems.contains(destination))
		return refuse(
		    Reason::ArrivalUndefined,
		    {Sim::ReasonArgument::Identifier(m_galaxy->systems[exit->destination].name)});

	const OnLane lane{
	    inSystem->frame, command.jumpPoint, destination, std::string{exit->arrival},
	    world.Clock().Step() + Jettison::Transit::Steps(exit->transitSeconds, world.FixedStep())};
	registry.emplace_or_replace<OnLane>(command.entity, lane);
	// Where it was going is in the system it left.
	Jettison::Transit::Depart(registry, command.entity);
	world.Events().Emit(LaneEntered{command.entity, lane, exit->transitSeconds});
}


void LaneTravel::Arrive(Sim::SimWorld &world) const
{
	entt::registry &registry = world.Registry();
	// The step being run ends at the next count.
	const std::vector<entt::entity> arriving =
	    Jettison::Transit::Arriving<OnLane>(registry, world.Clock().Step() + 1);
	if (arriving.empty())
		return;

	// Every arrival is placed before any is made, so a step that cannot place one changes nothing.
	const double endsAt = world.Clock().Now() + world.FixedStep();
	std::vector<std::uint64_t> destinations;
	std::vector<Jettison::Transit::Exit> exits;
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
		destinations.push_back(lane.to);
		// Out ahead of the ring, clear of its sensor, so the gate does not see it pass through.
		exits.push_back(
		    {{Celestial::EvaluateBody(starSystem, body, endsAt).metres, facing},
		     atStation ? starSystem.bodies[static_cast<std::size_t>(parent)].name : lane.arrival,
		     Gates::kRingDepth / 2.0});
	}
	Jettison::Transit::ComeOut(registry, m_physics, arriving, destinations, exits, kArrivalClearance);

	for (const entt::entity entity : arriving)
	{
		const OnLane lane = registry.get<const OnLane>(entity);
		registry.remove<OnLane>(entity);
		world.Events().Emit(LaneLeft{entity, lane.to, lane.arrival});
	}
}
} // namespace Jettison::Space
