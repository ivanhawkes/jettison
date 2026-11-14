#include <jettison/ai/Gates.h>

#include <jettison/ai/Cruise.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>
#include <jettison/celestial/Galaxy.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/physics/Shape.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>

#include "Transit.h"

namespace Jettison::Ai
{
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
	const auto *shipSystem = registry.try_get<Sim::InSystem>(ship);
	const auto *gateSystem = registry.try_get<Sim::InSystem>(gate);
	return shipSystem && gateSystem && shipSystem->system == gateSystem->system &&
	       shipSystem->system != Sim::kNoSystem && FrameOf(registry, ship) == FrameOf(registry, gate);
}

// Entities of a component in entity order, so what is done to them is the same however the storage
// happens to be laid out, as it is not after a load.
template <typename Component> std::vector<entt::entity> InOrder(const entt::registry &registry)
{
	const auto view = registry.view<const Component>();
	std::vector<entt::entity> entities{view.begin(), view.end()};
	std::ranges::sort(entities, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	return entities;
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
	         .Writing<Gate, Transit, OnLane, Sim::InSystem, Destination>(),
	     {std::string{LaneTravel::kSystemName}},
	     [gates, &world](entt::registry & /*registry*/, double /*fixedStep*/) { gates->Step(world); }});
	return *gates;
}

std::string Gates::RingShape(double radius)
{
	return std::format("rings/{}", radius);
}

void Gates::DefineStarSystem(const Celestial::StarSystem &system)
{
	const std::uint64_t id = Sim::SystemId(system.name);
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
	for (const auto [entity, gate, inSystem] : registry.view<const Gate, const Sim::InSystem>().each())
		if (inSystem.system == system)
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
		registry.emplace<Gate>(gate, Gate{body.name, entt::null, 0, {}});
		registry.emplace<Sim::Transform>(
		    gate, m_physics.RailsPlace(system, rails, world.Clock().Now()));
		registry.emplace<Sim::InSystem>(gate, system);
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
	for (const auto [entity, gate, inSystem] : registry.view<const Gate, const Sim::InSystem>().each())
		if (inSystem.system == system && gate.jumpPoint == jumpPoint)
			return entity;
	return entt::null;
}

const Celestial::Body *Gates::JumpPointOf(const entt::registry &registry, entt::entity gate) const
{
	const auto *inSystem = registry.try_get<Sim::InSystem>(gate);
	const auto system = inSystem ? m_systems.find(inSystem->system) : m_systems.end();
	if (system == m_systems.end())
		return nullptr;
	const int body = system->second.Find(registry.get<Gate>(gate).jumpPoint);
	return body < 0 ? nullptr : &system->second.bodies[static_cast<std::size_t>(body)];
}

std::optional<std::int64_t> Gates::TollThrough(const entt::registry &registry, entt::entity gate) const
{
	const Celestial::Galaxy *galaxy = m_lanes.Galaxy();
	const auto *inSystem = registry.try_get<Sim::InSystem>(gate);
	if (!galaxy || !inSystem)
		return std::nullopt;
	const auto system = std::ranges::find(
	    galaxy->systems, inSystem->system,
	    [](const Celestial::GalaxySystem &entry) { return Sim::SystemId(entry.name); });
	if (system == galaxy->systems.end())
		return std::nullopt;
	const std::optional<Celestial::Exit> exit = Celestial::ExitThrough(
	    *galaxy, static_cast<std::size_t>(system - galaxy->systems.begin()),
	    registry.get<Gate>(gate).jumpPoint);
	if (!exit || !m_lanes.HasStarSystem(Sim::SystemId(galaxy->systems[exit->destination].name)))
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
	const auto *inSystem = registry.try_get<Sim::InSystem>(ship);
	if (!inSystem || inSystem->system == Sim::kNoSystem)
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
		    held->cleared ? Reason::AlreadyCleared : Reason::AlreadyQueued,
		    {Named(registry, held->gate)}));
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

	Gate &gate = registry.get<Gate>(command.gate);
	if (gate.cleared == entt::null && gate.queue.empty())
		return Clear(world, command.gate, ship, *toll);
	gate.queue.push_back(ship);
	registry.emplace<Transit>(ship, Transit{command.gate, false});
	world.Events().Emit(
	    TransitQueued{ship, command.gate, static_cast<std::uint32_t>(gate.queue.size())});
}

void Gates::Clear(Sim::SimWorld &world, entt::entity gate, entt::entity ship, std::int64_t toll)
{
	entt::registry &registry = world.Registry();
	Gate &state = registry.get<Gate>(gate);
	state.cleared = ship;
	state.lapses = world.Clock().Step() + Detail::TransitSteps(kClearanceSeconds, world.FixedStep());
	registry.emplace_or_replace<Transit>(ship, Transit{gate, true});
	world.Events().Emit(TransitCleared{ship, gate, toll, state.lapses});
}

void Gates::GiveUp(entt::registry &registry, entt::entity ship)
{
	const auto *transit = registry.try_get<Transit>(ship);
	if (!transit)
		return;
	if (auto *gate = registry.valid(transit->gate) ? registry.try_get<Gate>(transit->gate) : nullptr)
	{
		if (gate->cleared == ship)
			gate->cleared = entt::null;
		std::erase(gate->queue, ship);
	}
	registry.remove<Transit>(ship);
}

void Gates::Send(Sim::SimWorld &world, entt::entity gate, entt::entity ship)
{
	entt::registry &registry = world.Registry();
	const std::string jumpPoint = registry.get<Gate>(gate).jumpPoint;
	const std::int64_t toll = TollThrough(registry, gate).value_or(0);
	GiveUp(registry, ship);
	if (std::optional<Sim::Reason> reason = Refuses(registry, ship, toll))
	{
		world.Events().Emit(GatePassed{ship, gate, std::move(*reason)});
		return;
	}
	// The lanes refuse it with a reason of their own, should they.
	world.Apply(EnterLane{ship, jumpPoint});
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
	while (registry.get<Gate>(gate).cleared == entt::null && !registry.get<Gate>(gate).queue.empty())
	{
		const entt::entity ship = registry.get<Gate>(gate).queue.front();
		GiveUp(registry, ship);
		if (std::optional<Sim::Reason> reason =
		        toll ? Refuses(registry, ship, *toll)
		             : Sim::MakeReason(Reason::LeadsNowhere, {Named(registry, gate)}))
			world.Events().Emit(TransitRefused{ship, gate, std::move(*reason)});
		else
			Clear(world, gate, ship, *toll);
	}
}

void Gates::Step(Sim::SimWorld &world)
{
	entt::registry &registry = world.Registry();

	// 1. What a ship can no longer hold.
	for (const entt::entity gate : InOrder<Gate>(registry))
	{
		Gate &state = registry.get<Gate>(gate);
		const auto gone = [&](entt::entity ship) { return !registry.valid(ship); };
		for (const entt::entity ship : state.queue)
			if (gone(ship))
				world.Events().Emit(TransitLapsed{ship, gate});
		std::erase_if(state.queue, gone);
		if (state.cleared != entt::null && gone(state.cleared))
		{
			world.Events().Emit(TransitLapsed{state.cleared, gate});
			state.cleared = entt::null;
		}
	}
	for (const entt::entity ship : InOrder<Transit>(registry))
	{
		const entt::entity gate = registry.get<Transit>(ship).gate;
		if (registry.valid(gate) && registry.all_of<Gate>(gate) && AtGate(registry, ship, gate) &&
		    !registry.all_of<Cruising>(ship))
			continue;
		GiveUp(registry, ship);
		world.Events().Emit(TransitLapsed{ship, gate});
	}

	// 2. Through the rings: each cleared ship touching its gate's sent, and every other body that has
	// begun to touch one passing through.
	const std::vector<entt::entity> gates = InOrder<Gate>(registry);
	std::vector<std::pair<entt::entity, entt::entity>> sent;
	for (const entt::entity gate : gates)
		if (const entt::entity ship = registry.get<Gate>(gate).cleared;
		    ship != entt::null && std::ranges::binary_search(m_physics.Touching(gate), ship))
		{
			sent.emplace_back(gate, ship);
			Send(world, gate, ship);
		}
	for (const Physics::ContactEvent &contact : m_physics.Contacts())
	{
		if (contact.phase != Physics::ContactPhase::Began || !contact.sensor)
			continue;
		const bool firstIsGate = registry.valid(contact.first) && registry.all_of<Gate>(contact.first);
		const entt::entity gate = firstIsGate ? contact.first : contact.second;
		const entt::entity ship = firstIsGate ? contact.second : contact.first;
		if (!registry.valid(gate) || !registry.all_of<Gate>(gate) ||
		    std::ranges::find(sent, std::pair{gate, ship}) != sent.end())
			continue;
		world.Events().Emit(
		    GatePassed{ship, gate, Sim::MakeReason(Reason::NotCleared, {Named(registry, gate)})});
	}

	// 3. Clearances lapsed unused, as the step ends.
	const std::uint64_t ending = world.Clock().Step() + 1;
	for (const entt::entity gate : gates)
		if (const Gate &state = registry.get<Gate>(gate);
		    state.cleared != entt::null && state.lapses <= ending)
		{
			const entt::entity ship = state.cleared;
			GiveUp(registry, ship);
			world.Events().Emit(TransitLapsed{ship, gate});
		}

	// 4. The next in each free gate's queue.
	for (const entt::entity gate : gates)
		Serve(world, gate);
}
} // namespace Jettison::Ai
