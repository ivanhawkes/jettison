#include <jettison/ai/Lanes.h>

#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Jettison::Ai
{
namespace
{
// A transit in whole steps, rounded up, and at least one. A millionth of a step is let go before
// rounding up, since a transit authored in seconds is rarely a whole number of steps in floating
// point: 40 s at 60 Hz is 2400.0000000000005 steps.
std::uint64_t TransitSteps(double transitSeconds, double fixedStep)
{
	const double steps = std::ceil((transitSeconds / fixedStep) - 1.0e-6);
	return std::max<std::uint64_t>(1, steps > 0.0 ? static_cast<std::uint64_t>(steps) : 0);
}
} // namespace

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Physics::RegisterTypes();
		RegisterType<OnLane>("Ai.OnLane")
		    .Field<&OnLane::from>("from", Attribute::Saved)
		    .Field<&OnLane::departure>("departure", Attribute::Saved)
		    .Field<&OnLane::to>("to", Attribute::Saved)
		    .Field<&OnLane::arrival>("arrival", Attribute::Saved)
		    .Field<&OnLane::arrivalStep>("arrivalStep", Attribute::Saved);
		RegisterType<EnterLane>("Ai.EnterLane")
		    .Field<&EnterLane::entity>("entity", Attribute::Saved)
		    .Field<&EnterLane::jumpPoint>("jumpPoint", Attribute::Saved);
		RegisterType<LaneEntered>("Ai.LaneEntered")
		    .Field<&LaneEntered::entity>("entity", Attribute::Saved)
		    .Field<&LaneEntered::lane>("lane", Attribute::Saved)
		    .Field<&LaneEntered::transitSeconds>("transitSeconds", Attribute::Saved);
		RegisterType<LaneLeft>("Ai.LaneLeft")
		    .Field<&LaneLeft::entity>("entity", Attribute::Saved)
		    .Field<&LaneLeft::system>("system", Attribute::Saved)
		    .Field<&LaneLeft::arrival>("arrival", Attribute::Saved);
		RegisterType<LaneRefused>("Ai.LaneRefused")
		    .Field<&LaneRefused::entity>("entity", Attribute::Saved)
		    .Field<&LaneRefused::jumpPoint>("jumpPoint", Attribute::Saved)
		    .Field<&LaneRefused::reason>("reason", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

LaneTravel &LaneTravel::Attach(Sim::SimWorld &world)
{
	RegisterTypes();
	// Shared by the command and the system, which the world keeps for as long as it lives.
	std::shared_ptr<LaneTravel> lanes{new LaneTravel};
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
	const auto refuse = [&](std::string reason)
	{ world.Events().Emit(LaneRefused{command.entity, command.jumpPoint, std::move(reason)}); };
	if (!registry.valid(command.entity))
		return refuse("there is no such entity");
	const auto *inSystem = registry.try_get<Sim::InSystem>(command.entity);
	if (!inSystem || inSystem->system == Sim::kNoSystem)
		return refuse(
		    registry.all_of<OnLane>(command.entity) ? "it is on a lane already" : "it is in no system");
	if (!m_galaxy)
		return refuse("there is no galaxy");
	const auto system = std::ranges::find(
	    m_galaxy->systems, inSystem->system,
	    [](const Celestial::GalaxySystem &entry) { return Sim::SystemId(entry.name); });
	if (system == m_galaxy->systems.end())
		return refuse("its system is not one of the galaxy's");
	const std::optional<Celestial::Exit> exit = Celestial::ExitThrough(
	    *m_galaxy, static_cast<std::size_t>(system - m_galaxy->systems.begin()), command.jumpPoint);
	if (!exit)
		return refuse(std::format("no lane leaves '{}' through '{}'", system->name, command.jumpPoint));
	const std::uint64_t destination = Sim::SystemId(m_galaxy->systems[exit->destination].name);
	if (!m_systems.contains(destination))
		return refuse(
		    std::format(
		        "the lane arrives in '{}', which is not defined",
		        m_galaxy->systems[exit->destination].name));

	const OnLane lane{
	    inSystem->system, command.jumpPoint, destination, std::string{exit->arrival},
	    world.Clock().Step() + TransitSteps(exit->transitSeconds, world.FixedStep())};
	registry.emplace_or_replace<OnLane>(command.entity, lane);
	registry.replace<Sim::InSystem>(command.entity, Sim::kNoSystem);
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
		places.push_back(
		    {{Celestial::EvaluateBody(starSystem, body, endsAt).metres,
		      Celestial::JumpPointFacing(starSystem, body, endsAt)},
		     atStation ? starSystem.bodies[static_cast<std::size_t>(parent)].name : lane.arrival});
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
