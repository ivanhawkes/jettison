#include <jettison/transit/Portal.h>

#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/transit/Passage.h>
#include <jettison/transit/Path.h>
#include <jettison/transit/Types.h>

#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Jettison::Transit
{
void Depart(entt::registry &registry, entt::entity entity)
{
	registry.replace<Sim::InFrame>(entity, Sim::kNoFrame);
	GiveUpDestination(registry, entity);
}


void ComeOut(
    entt::registry &registry, const Physics::PhysicsWorld &physics,
    std::span<const entt::entity> arriving, std::span<const std::uint64_t> frames,
    std::span<Exit> exits, double clearance)
{
	// What is already in each frame arrived in, where it is as the step ends; each arrival joins
	// them once it is placed.
	std::vector<Occupant> occupants = Occupants(registry, physics, frames);
	for (std::size_t index = 0; index < arriving.size(); ++index)
	{
		const auto *body = registry.try_get<const Physics::Body>(arriving[index]);
		if (!body)
			continue;
		const double radius = physics.ShapeRadius(body->shape);
		Sim::Transform &place = exits[index].place;
		place.position +=
		    place.orientation * glm::dvec3{0.0, 0.0, -(exits[index].ahead + radius + clearance)};
		place.position = ClearPlace(occupants, frames[index], place, radius, clearance);
		occupants.push_back({frames[index], place.position, radius});
	}

	for (std::size_t index = 0; index < arriving.size(); ++index)
	{
		const entt::entity entity = arriving[index];
		registry.emplace_or_replace<Sim::Transform>(entity, exits[index].place);
		registry.emplace_or_replace<Sim::InFrame>(entity, frames[index]);
		if (registry.all_of<Physics::Body>(entity))
		{
			registry.emplace_or_replace<Physics::LocalFrame>(entity, exits[index].localFrame);
			registry.emplace_or_replace<Physics::Velocity>(entity);
		}
		if (auto *controls = registry.try_get<Physics::FlightControls>(entity))
			controls->reference = entt::null;
	}
}


Portals &Portals::Attach(Sim::SimWorld &world, const Physics::PhysicsWorld &physics, Locate locate)
{
	if (!locate)
		throw std::invalid_argument{"Transit: portals need the game's exits placed"};
	RegisterTypes();

	// Shared by the system, which the world keeps for as long as it lives.
	std::shared_ptr<Portals> portals{new Portals{physics, std::move(locate)}};
	world.RegisterComponent<InPortal>();
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Physics::Body>()
	         .Writing<
	             InPortal, Sim::InFrame, Sim::Transform, Physics::LocalFrame, Physics::Velocity,
	             Physics::FlightControls>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [portals, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	     { portals->Arrive(world); }},
	    {.next = NextArrival<InPortal>});
	return *portals;
}


void Portals::Enter(
    Sim::SimWorld &world, entt::entity entity, std::string entrance, std::uint64_t to, std::string exit,
    double seconds)
{
	entt::registry &registry = world.Registry();
	const auto *inFrame = registry.valid(entity) ? registry.try_get<Sim::InFrame>(entity) : nullptr;
	if (!inFrame || inFrame->frame == Sim::kNoFrame)
		throw std::invalid_argument{std::format(
		    "Transit: entity {:#x} cannot go through '{}': it is in no frame",
		    entt::to_integral(entity), entrance)};
	const InPortal portal{
	    inFrame->frame, std::move(entrance), to, std::move(exit),
	    world.Clock().Step() + Steps(seconds, world.FixedStep())};
	registry.emplace<InPortal>(entity, portal);
	Depart(registry, entity);
}


void Portals::Arrive(Sim::SimWorld &world) const
{
	entt::registry &registry = world.Registry();

	// The step being run ends at the next count, and at this time.
	const std::vector<entt::entity> arriving = Arriving<InPortal>(registry, world.Clock().Step() + 1);
	if (arriving.empty())
		return;
	const double endsAt = world.Clock().Now() + world.FixedStep();

	// Every exit is placed before any arrival is made, so one the game does not have changes nothing.
	std::vector<std::uint64_t> frames;
	std::vector<Exit> exits;
	for (const entt::entity entity : arriving)
	{
		const InPortal &portal = registry.get<const InPortal>(entity);
		frames.push_back(portal.to);
		exits.push_back(m_locate(portal.to, portal.exit, endsAt));
	}
	ComeOut(registry, m_physics, arriving, frames, exits, kArrivalClearance);
	for (const entt::entity entity : arriving)
	{
		const InPortal portal = registry.get<const InPortal>(entity);
		registry.remove<InPortal>(entity);
		world.Events().Emit(PortalLeft{entity, portal.to, portal.exit});
	}
}
} // namespace Jettison::Transit
