#include <jettison/transit/Passage.h>

#include <jettison/ai/Steering.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/transit/Path.h>
#include <jettison/transit/Types.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Jettison::Transit
{
void GiveUpDestination(entt::registry &registry, entt::entity entity)
{
	if (registry.remove<Ai::Destination>(entity) == 0)
		return;
	if (auto *controls = registry.try_get<Physics::FlightControls>(entity))
		*controls = Physics::FlightControls{};
}


Physics::Body TakeOut(entt::registry &registry, entt::entity entity)
{
	Physics::Body body = registry.get<const Physics::Body>(entity);
	registry.remove<Physics::Body, Physics::Velocity, Physics::LocalFrame>(entity);
	GiveUpDestination(registry, entity);
	return body;
}


void BringOut(
    entt::registry &registry, entt::entity entity, const Sim::Transform &place,
    const Physics::Body &body, std::string frame)
{
	registry.emplace_or_replace<Sim::Transform>(entity, place);
	registry.emplace_or_replace<Physics::Body>(entity, body);
	registry.emplace_or_replace<Physics::LocalFrame>(entity, std::move(frame));
	registry.emplace_or_replace<Physics::Velocity>(entity);
	if (auto *controls = registry.try_get<Physics::FlightControls>(entity))
		controls->reference = entt::null;
}


std::vector<Occupant> Occupants(
    const entt::registry &registry, const Physics::PhysicsWorld &physics,
    std::span<const std::uint64_t> frames)
{
	std::vector<Occupant> occupants;
	for (const auto [entity, body, inFrame, transform] :
	     registry.view<const Physics::Body, const Sim::InFrame, const Sim::Transform>().each())
		if (!body.sensor && std::ranges::find(frames, inFrame.frame) != frames.end())
			occupants.push_back({inFrame.frame, transform.position, physics.ShapeRadius(body.shape)});
	return occupants;
}


glm::dvec3 ClearPlace(
    std::span<const Occupant> occupants, std::uint64_t frame, const Sim::Transform &place,
    double radius, double clearance)
{
	const auto clear = [&](const glm::dvec3 &at)
	{
		return std::ranges::none_of(
		    occupants,
		    [&](const Occupant &occupant)
		    {
			    return occupant.frame == frame &&
			           glm::distance(occupant.position, at) < occupant.radius + radius + clearance;
		    });
	};
	if (clear(place.position))
		return place.position;
	// Six places on the first ring, twelve on the second and so on.
	constexpr int kRingPlaces = 6;
	const double spacing = 2.0 * radius + clearance;
	const glm::dvec3 right = place.orientation * glm::dvec3{1.0, 0.0, 0.0};
	const glm::dvec3 up = place.orientation * glm::dvec3{0.0, 1.0, 0.0};
	for (int ring = 1;; ++ring)
		for (int slot = 0; slot < kRingPlaces * ring; ++slot)
		{
			const double angle = glm::two_pi<double>() * static_cast<double>(slot) /
			                     static_cast<double>(kRingPlaces * ring);
			const glm::dvec3 at = place.position + (right * std::cos(angle) + up * std::sin(angle)) *
			                                           (spacing * static_cast<double>(ring));
			if (clear(at))
				return at;
		}
}


Passages &Passages::Attach(Sim::SimWorld &world, const Physics::PhysicsWorld &physics, Locate locate)
{
	RegisterTypes();
	// Shared by the system and its quiet, which the world keeps for as long as it lives.
	std::shared_ptr<Passages> passages{new Passages{physics, std::move(locate)}};
	world.RegisterComponent<Passage>();
	world.ShowInSnapshots<Passage>();
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Sim::InFrame>()
	         .Writing<
	             Passage, Sim::Transform, Physics::Body, Physics::LocalFrame, Physics::Velocity,
	             Physics::FlightControls>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [passages, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	     { passages->Place(world); }},
	    // Until the step that places an arrival, each entity is where its way has it.
	    {.next =
	         [](const Sim::SimWorld &target)
	     {
		     std::uint64_t next = std::numeric_limits<std::uint64_t>::max();
		     for (const auto [entity, passage] : target.Registry().view<const Passage>().each())
			     next = std::min(
			         next, passage.arrivalStep - std::min<std::uint64_t>(passage.arrivalStep, 1));
		     return next;
	     },
	     .skip = [passages](Sim::SimWorld &target, std::uint64_t /*passed*/, double /*lastBegan*/)
	     { passages->Skip(target); }});
	return *passages;
}


void Passages::Begin(Sim::SimWorld &world, entt::entity entity, Passage passage, double seconds)
{
	entt::registry &registry = world.Registry();
	if (!registry.valid(entity) ||
	    !registry.all_of<Physics::Body, Sim::Transform, Sim::InFrame>(entity) ||
	    registry.all_of<Passage>(entity) || passage.path.empty())
		throw std::invalid_argument{std::format(
		    "Transit: entity {:#x} cannot set out: it needs a body, a place and a frame, no passage "
		    "already, and a way with a point",
		    entt::to_integral(entity))};
	passage.departureStep = world.Clock().Step();
	passage.arrivalStep = passage.departureStep + Steps(seconds, world.FixedStep());
	passage.body = TakeOut(registry, entity);
	registry.emplace<Passage>(entity, std::move(passage));
}


glm::dvec3 Passages::Origin(std::uint64_t frame, const Passage &passage, double time) const
{
	if (passage.carrier.empty())
		return glm::dvec3{0.0};
	if (!m_locate)
		throw std::invalid_argument{std::format(
		    "Transit: no carrier is placed, and a passage is carried by '{}'", passage.carrier)};
	return m_locate(frame, passage.carrier, time);
}


void Passages::Skip(Sim::SimWorld &world) const
{
	entt::registry &registry = world.Registry();
	// As the last step passed over placed them: it ended at the clock's count and time.
	const std::uint64_t ending = world.Clock().Step();
	const double endsAt = world.Clock().Now();
	for (const auto [entity, passage, inFrame] :
	     registry.view<const Passage, const Sim::InFrame>().each())
		registry.emplace_or_replace<Sim::Transform>(
		    entity, Underway(
		                passage.path, passage.departureStep, passage.arrivalStep, passage.profile,
		                Origin(inFrame.frame, passage, endsAt), ending));
}


void Passages::Place(Sim::SimWorld &world) const
{
	entt::registry &registry = world.Registry();
	std::vector<entt::entity> entities;
	for (const entt::entity entity : registry.view<const Passage>())
		entities.push_back(entity);
	if (entities.empty())
		return;
	std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });

	// The step being run ends at the next count, and at this time.
	const std::uint64_t ending = world.Clock().Step() + 1;
	const double endsAt = world.Clock().Now() + world.FixedStep();
	// What each arrival comes out clear of, gathered at the first.
	std::optional<std::vector<Occupant>> occupants;
	for (const entt::entity entity : entities)
	{
		const Passage &passage = registry.get<const Passage>(entity);
		const std::uint64_t frame = registry.get<const Sim::InFrame>(entity).frame;
		const glm::dvec3 origin = Origin(frame, passage, endsAt);
		if (ending < passage.arrivalStep)
		{
			registry.emplace_or_replace<Sim::Transform>(
			    entity, Underway(
			                passage.path, passage.departureStep, passage.arrivalStep, passage.profile,
			                origin, ending));
			continue;
		}

		if (!occupants)
		{
			std::vector<std::uint64_t> frames;
			frames.reserve(entities.size());
			for (const entt::entity other : entities)
				frames.push_back(registry.get<const Sim::InFrame>(other).frame);
			occupants = Occupants(registry, m_physics, frames);
		}
		Sim::Transform place{origin + passage.path.back(), Facing(Along(passage.path, 1.0).second)};
		const double radius = m_physics.ShapeRadius(passage.body.shape);
		place.position = ClearPlace(*occupants, frame, place, radius, kArrivalClearance);
		occupants->push_back({frame, place.position, radius});
		BringOut(registry, entity, place, passage.body, passage.frame);
		registry.remove<Passage>(entity);
		world.Events().Emit(PassageEnded{entity});
	}
}
} // namespace Jettison::Transit
