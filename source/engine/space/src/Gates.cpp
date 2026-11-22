#include <jettison/space/Gates.h>

#include <jettison/ai/Steering.h>
#include <jettison/celestial/Galaxy.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/physics/Shape.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Lanes.h>
#include <jettison/space/Types.h>
#include <jettison/transit/Path.h>
#include <jettison/transit/Slot.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>


namespace Jettison::Space
{
using Ai::Destination;
using Ai::Steered;

namespace
{
// The frame an entity's body is in: its LocalFrame's body, or the system's own (empty).
std::string_view FrameOf(const entt::registry &registry, entt::entity entity)
{
	const auto *frame = registry.try_get<Physics::LocalFrame>(entity);
	return frame ? std::string_view{frame->body} : std::string_view{};
}


// Whether the ship is in the gate's system and frame, where its ring can meet it.
bool AtGate(const entt::registry &registry, entt::entity ship, entt::entity gate)
{
	const auto *shipSystem = registry.try_get<Sim::InFrame>(ship);
	const auto *gateSystem = registry.try_get<Sim::InFrame>(gate);
	return shipSystem && gateSystem && shipSystem->frame == gateSystem->frame &&
	       shipSystem->frame != Sim::kNoFrame && FrameOf(registry, ship) == FrameOf(registry, gate);
}


Sim::ReasonArgument Named(const entt::registry &registry, entt::entity gate)
{
	const auto *found = registry.valid(gate) ? registry.try_get<Gate>(gate) : nullptr;
	return Sim::ReasonArgument::Identifier(found ? found->jumpPoint : std::string{});
}
} // namespace


Gates &Gates::Attach(Sim::SimWorld &world, Physics::PhysicsWorld &physics, const LaneTravel &lanes)
{
	RegisterTypes();
	// Shared by the command and the system, which the world keeps for as long as it lives.
	std::shared_ptr<Gates> gates{new Gates{physics, lanes}};
	world.RegisterComponent<Gate>();
	world.RegisterComponent<Transit>();
	world.ShowInSnapshots<Gate>();
	world.ShowInSnapshots<Transit>();
	world.RegisterCommand<RequestTransit>([gates](Sim::SimWorld &target, const RequestTransit &command)
	                                      { gates->Request(target, command); });
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Physics::Body, Physics::LocalFrame, Cruising>()
	         .Writing<Gate, Transit, OnLane, Sim::InFrame, Destination>(),
	     {std::string{LaneTravel::kSystemName}},
	     [gates, &world](entt::registry & /*registry*/, double /*fixedStep*/) { gates->Step(world); }},
	    // A ship at a gate is this step's, and the slots' changes are theirs.
	    {.next = [](const Sim::SimWorld &target)
	     {
		     return target.Registry().view<const Transit>().empty()
		                ? Jettison::Transit::NextLapse<Gate>(target)
		                : target.Clock().Step();
	     }});
	return *gates;
}


std::string Gates::RingShape(double radius)
{
	return std::format("rings/{}", radius);
}


void Gates::DefineStarSystem(const Celestial::StarSystem &system)
{
	const std::uint64_t id = Sim::FrameId(system.name);
	if (m_systems.contains(id))
		throw std::invalid_argument{
		    std::format("Ai: star system '{}' is already defined", system.name)};
	for (const Celestial::Body &body : system.bodies)
	{
		if (body.kind != Celestial::BodyKind::JumpPoint)
			continue;
		if (!(body.radius > 0.0))
			throw std::invalid_argument{std::format(
			    "Ai: jump point '{}' of '{}' has no ring to fly through", body.name, system.name)};
		if (const std::string shape = RingShape(body.radius); !m_physics.HasShape(shape))
		{
			Physics::ShapeDefinition ring;
			ring.kind = Physics::ShapeKind::Cylinder;
			ring.radius = static_cast<float>(body.radius);
			ring.halfHeight = static_cast<float>(kRingDepth / 2.0);
			m_physics.DefineShape(shape, ring);
		}
	}
	m_systems.emplace(id, system);
}


bool Gates::HasStarSystem(std::uint64_t system) const
{
	return m_systems.contains(system);
}


void Gates::SetTollRule(TollRule rule)
{
	m_rule = std::move(rule);
}


std::vector<entt::entity> Gates::MakeGates(Sim::SimWorld &world, std::uint64_t system) const
{
	const auto found = m_systems.find(system);
	if (found == m_systems.end())
		throw std::invalid_argument{
		    std::format("Ai: gates for system {:#x}, which is not defined", system)};
	entt::registry &registry = world.Registry();
	for (const auto [entity, gate, inSystem] : registry.view<const Gate, const Sim::InFrame>().each())
		if (inSystem.frame == system)
			throw std::logic_error{
			    std::format("Ai: the gates of '{}' are already made", found->second.name)};

	// The ring lies across the cylinder's Y axis, stood up to face along the jump point's -Z.
	const glm::dquat standUp = glm::angleAxis(-glm::half_pi<double>(), glm::dvec3{1.0, 0.0, 0.0});
	std::vector<entt::entity> gates;
	for (const Celestial::Body &body : found->second.bodies)
	{
		if (body.kind != Celestial::BodyKind::JumpPoint)
			continue;
		const Physics::OnRails rails{body.name, glm::dvec3{0.0}, standUp};
		Physics::Body ring{Physics::BodyKind::Kinematic, RingShape(body.radius)};
		ring.sensor = true;
		const entt::entity gate = registry.create();
		registry.emplace<Gate>(gate, Gate{{}, body.name});
		registry.emplace<Sim::Transform>(
		    gate, m_physics.RailsPlace(system, rails, world.Clock().Now()));
		registry.emplace<Sim::InFrame>(gate, system);
		registry.emplace<Physics::OnRails>(gate, rails);
		registry.emplace<Physics::LocalFrame>(gate, body.name);
		registry.emplace<Physics::Body>(gate, std::move(ring));
		gates.push_back(gate);
	}
	return gates;
}


entt::entity Gates::FindGate(
    const entt::registry &registry, std::uint64_t system, std::string_view jumpPoint)
{
	for (const auto [entity, gate, inSystem] : registry.view<const Gate, const Sim::InFrame>().each())
		if (inSystem.frame == system && gate.jumpPoint == jumpPoint)
			return entity;
	return entt::null;
}


const Celestial::Body *Gates::JumpPointOf(const entt::registry &registry, entt::entity gate) const
{
	const auto *inSystem = registry.try_get<Sim::InFrame>(gate);
	const auto system = inSystem ? m_systems.find(inSystem->frame) : m_systems.end();
	if (system == m_systems.end())
		return nullptr;
	const int body = system->second.Find(registry.get<Gate>(gate).jumpPoint);
	return body < 0 ? nullptr : &system->second.bodies[static_cast<std::size_t>(body)];
}


std::optional<std::int64_t> Gates::TollThrough(const entt::registry &registry, entt::entity gate) const
{
	const Celestial::Galaxy *galaxy = m_lanes.Galaxy();
	const auto *inSystem = registry.try_get<Sim::InFrame>(gate);
	if (!galaxy || !inSystem)
		return std::nullopt;
	const auto system = std::ranges::find(
	    galaxy->systems, inSystem->frame,
	    [](const Celestial::GalaxySystem &entry) { return Sim::FrameId(entry.name); });
	if (system == galaxy->systems.end())
		return std::nullopt;
	const std::optional<Celestial::Exit> exit = Celestial::ExitThrough(
	    *galaxy, static_cast<std::size_t>(system - galaxy->systems.begin()),
	    registry.get<Gate>(gate).jumpPoint);
	if (!exit || !m_lanes.HasStarSystem(Sim::FrameId(galaxy->systems[exit->destination].name)))
		return std::nullopt;
	return exit->toll;
}


std::optional<Sim::Reason> Gates::Refuses(
    const entt::registry &registry, entt::entity ship, std::int64_t toll) const
{
	return m_rule.refuses ? m_rule.refuses(registry, ship, toll) : std::nullopt;
}


void Gates::Request(Sim::SimWorld &world, const RequestTransit &command)
{
	entt::registry &registry = world.Registry();
	const entt::entity ship = command.ship;
	const auto refuse = [&](Sim::Reason reason)
	{ world.Events().Emit(TransitRefused{ship, command.gate, std::move(reason)}); };
	const auto because = [](Reason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{ return Sim::MakeReason(reason, std::move(arguments)); };

	if (!registry.valid(ship))
		return refuse(because(Reason::NoSuchEntity));
	if (!Sim::MayCommand(registry, ship, command.sender))
		return refuse(because(Reason::NotController));
	if (!registry.valid(command.gate) || !registry.all_of<Gate>(command.gate))
		return refuse(because(Reason::NotAGate));
	const auto *inSystem = registry.try_get<Sim::InFrame>(ship);
	if (!inSystem || inSystem->frame == Sim::kNoFrame)
		return refuse(because(registry.all_of<OnLane>(ship) ? Reason::OnLane : Reason::InNoSystem));
	if (registry.all_of<Cruising>(ship))
		return refuse(because(Reason::AlreadyCruising));
	const auto *body = registry.try_get<Physics::Body>(ship);
	if (!body || body->kind != Physics::BodyKind::Dynamic || registry.all_of<Physics::OnRails>(ship))
		return refuse(because(Reason::CannotTransit));
	const Sim::ReasonArgument gateName = Named(registry, command.gate);
	if (!AtGate(registry, ship, command.gate))
		return refuse(because(Reason::NotAtGate, {gateName}));
	if (const auto *held = registry.try_get<Transit>(ship))
		return refuse(because(
		    held->held ? Reason::AlreadyCleared : Reason::AlreadyQueued,
		    {Named(registry, held->slot)}));
	const Celestial::Body *jumpPoint = JumpPointOf(registry, command.gate);
	if (!jumpPoint)
		return refuse(because(Reason::SystemUndefined));
	const std::optional<std::int64_t> toll = TollThrough(registry, command.gate);
	if (!toll)
		return refuse(because(Reason::LeadsNowhere, {gateName}));
	if (m_physics.ShapeRadius(body->shape) > jumpPoint->radius)
		return refuse(because(
		    Reason::HullTooLarge,
		    {gateName, Sim::ReasonArgument::Integer(std::llround(jumpPoint->radius))}));
	if (std::optional<Sim::Reason> reason = Refuses(registry, ship, *toll))
		return refuse(std::move(*reason));

	if (Jettison::Transit::Free(registry.get<Gate>(command.gate)))
		return Clear(world, command.gate, ship, *toll);
	world.Events().Emit(
	    TransitQueued{
	        ship, command.gate, Jettison::Transit::Wait<Gate, Transit>(registry, command.gate, ship)});
}


void Gates::Clear(Sim::SimWorld &world, entt::entity gate, entt::entity ship, std::int64_t toll)
{
	const std::uint64_t lapses =
	    world.Clock().Step() + Jettison::Transit::Steps(kClearanceSeconds, world.FixedStep());
	Jettison::Transit::Hold<Gate, Transit>(world.Registry(), gate, ship, lapses);
	world.Events().Emit(TransitCleared{ship, gate, toll, lapses});
}


void Gates::Send(Sim::SimWorld &world, entt::entity gate, entt::entity ship)
{
	entt::registry &registry = world.Registry();
	const std::string jumpPoint = registry.get<Gate>(gate).jumpPoint;
	const std::int64_t toll = TollThrough(registry, gate).value_or(0);
	Jettison::Transit::GiveUp<Gate, Transit>(registry, ship);
	if (std::optional<Sim::Reason> reason = Refuses(registry, ship, toll))
	{
		world.Events().Emit(GatePassed{ship, gate, std::move(*reason)});
		return;
	}
	// The lanes refuse it with a reason of their own, should they.
	world.Apply(EnterLane{{}, ship, jumpPoint});
	if (!registry.all_of<OnLane>(ship))
		return;
	if (m_rule.charge)
		m_rule.charge(registry, ship, toll);
	world.Events().Emit(TransitSent{ship, gate, toll});
}


void Gates::Serve(Sim::SimWorld &world, entt::entity gate)
{
	entt::registry &registry = world.Registry();
	const std::optional<std::int64_t> toll = TollThrough(registry, gate);
	Jettison::Transit::Serve<Gate, Transit>(
	    registry, gate,
	    [&](entt::entity ship)
	    {
		    if (std::optional<Sim::Reason> reason =
		            toll ? Refuses(registry, ship, *toll)
		                 : Sim::MakeReason(Reason::LeadsNowhere, {Named(registry, gate)}))
			    world.Events().Emit(TransitRefused{ship, gate, std::move(*reason)});
		    else
			    Clear(world, gate, ship, *toll);
	    });
}


void Gates::Step(Sim::SimWorld &world)
{
	entt::registry &registry = world.Registry();
	const std::vector<entt::entity> gates = Jettison::Transit::InOrder<Gate>(registry);

	// 1. What a ship can no longer hold.
	for (const auto [ship, gate] : Jettison::Transit::DropGone<Gate>(registry, gates))
		world.Events().Emit(TransitLapsed{ship, gate});
	for (const entt::entity ship : Jettison::Transit::InOrder<Transit>(registry))
	{
		const entt::entity gate = registry.get<Transit>(ship).slot;
		if (registry.valid(gate) && registry.all_of<Gate>(gate) && AtGate(registry, ship, gate) &&
		    !registry.all_of<Cruising>(ship))
			continue;
		Jettison::Transit::GiveUp<Gate, Transit>(registry, ship);
		world.Events().Emit(TransitLapsed{ship, gate});
	}

	// 2. Through the rings: each cleared ship touching its gate's sent, and every other body that has
	// begun to touch one passing through.
	const Jettison::Transit::Crossings crossings =
	    Jettison::Transit::Crossed<Gate>(registry, m_physics, gates);
	for (const auto [gate, ship] : crossings.through)
		Send(world, gate, ship);
	for (const auto [gate, ship] : crossings.passed)
		world.Events().Emit(
		    GatePassed{ship, gate, Sim::MakeReason(Reason::NotCleared, {Named(registry, gate)})});

	// 3. Clearances lapsed unused, as the step ends.
	for (const auto [ship, gate] :
	     Jettison::Transit::Lapse<Gate, Transit>(registry, gates, world.Clock().Step() + 1))
		world.Events().Emit(TransitLapsed{ship, gate});

	// 4. The next in each free gate's queue.
	for (const entt::entity gate : gates)
		Serve(world, gate);
}
} // namespace Jettison::Space
