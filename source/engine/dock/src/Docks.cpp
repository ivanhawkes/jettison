#include <jettison/dock/Docks.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include <jettison/ai/Routes.h>
#include <jettison/ai/Steering.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/profile/Profile.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Sorted.h"

namespace Jettison::Dock
{
namespace
{
// A clamp's reach: how far across the pad from where the hull rests, and above or below it, a ship
// can be and be clamped, how far it can be turned from the pad, and how fast it can be moving.
// Clamping puts it where it rests, so these are what it may be moved by.
constexpr double kClampAcross = 2.0;
constexpr double kClampAbove = 1.0;
constexpr double kClampSpeed = 0.5;
const double kClampUp = std::cos(glm::radians(10.0));
const double kClampNose = std::cos(glm::radians(15.0));
// How far above its resting height a ship flown down aims to stop, a hand's breadth: the clamp
// takes it from there, and Jolt's contacts begin a couple of centimetres before two bodies meet, so
// a ship flown onto the deck itself would touch it first.
constexpr double kHover = 0.25;
// Beyond the station's sphere, the ship's hull and its clearance, the point above a pad is this far
// again, so a ship arriving there is outside what steering keeps it from, and not at its edge.
constexpr double kAboveMargin = 10.0;
// How a ship is flown through the stages: the point above its pad it arrives at, the speed it comes
// down and lifts at, and how near it stops above the pad.
constexpr float kAboveRadius = 20.0f;
constexpr float kAboveSpeed = 3.0f;
constexpr float kDescentSpeed = 40.0f;
constexpr float kLiftSpeed = 20.0f;
constexpr float kLandingRadius = 0.25f;
constexpr float kLandingSpeed = 0.2f;

using Detail::Sorted;

std::string_view FrameOf(const entt::registry &registry, entt::entity entity)
{
	const auto *frame = registry.try_get<Physics::LocalFrame>(entity);
	return frame ? std::string_view{frame->body} : std::string_view{};
}

std::uint64_t SystemOf(const entt::registry &registry, entt::entity entity)
{
	const auto *inSystem = registry.try_get<Sim::InSystem>(entity);
	return inSystem ? inSystem->system : Sim::kNoSystem;
}

// The station's name, as its celestial body has it, for what is said about it.
std::string_view NameOf(const entt::registry &registry, entt::entity station)
{
	const auto *rails = registry.try_get<Physics::OnRails>(station);
	return rails ? std::string_view{rails->body} : std::string_view{"the station"};
}

bool Fits(const HullDefinition &hull, const Pad &pad)
{
	return !pad.display && pad.padClass >= hull.pad;
}

// The ship's steering, as it keeps clear, given it by default if it has none: the docks fly it.
const Ai::Steered &Steer(entt::registry &registry, entt::entity ship)
{
	if (const auto *steered = registry.try_get<Ai::Steered>(ship))
		return *steered;
	return registry.emplace<Ai::Steered>(ship);
}

void Hold(entt::registry &registry, entt::entity ship)
{
	registry.remove<Ai::Destination>(ship);
	if (auto *controls = registry.try_get<Physics::FlightControls>(ship))
		*controls = Physics::FlightControls{};
}
} // namespace

bool IsDocked(const entt::registry &registry, entt::entity ship, entt::entity station)
{
	const auto *docking = registry.valid(ship) ? registry.try_get<Docking>(ship) : nullptr;
	return docking && docking->stage == Stage::Docked && docking->station == station;
}

Docks &Docks::Attach(Sim::SimWorld &world, Physics::PhysicsWorld &physics)
{
	RegisterTypes();
	// Shared by the commands and the system, which the world keeps for as long as it lives.
	std::shared_ptr<Docks> docks{new Docks{physics}};
	world.RegisterComponent<Hull>();
	world.RegisterComponent<Station>();
	world.RegisterComponent<Pad>();
	world.RegisterComponent<Docking>();
	world.RegisterComponent<Transfer>();
	world.RegisterComponent<Buggy>();
	// A pad's state is what its lamps show (the station dressing), a ship's stage and hull whether
	// and how its pad's clamps close on it, a buggy is drawn where it is with what it carries, and
	// presentation reads snapshots.
	world.ShowInSnapshots<Pad>();
	world.ShowInSnapshots<Docking>();
	world.ShowInSnapshots<Hull>();
	world.ShowInSnapshots<Transfer>();
	world.ShowInSnapshots<Buggy>();
	world.RegisterCommand<RequestPad>([docks](Sim::SimWorld &target, const RequestPad &command)
	                                  { docks->Request(target, command); });
	world.RegisterCommand<ReleasePad>([docks](Sim::SimWorld &target, const ReleasePad &command)
	                                  { docks->Release(target, command); });
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Hull, Physics::LocalFrame>()
	         .Writing<
	             Station, Pad, Docking, Transfer, Buggy, Physics::Body, Physics::OnRails,
	             Physics::Velocity, Physics::FlightControls, Sim::Transform, Sim::InSystem,
	             Ai::Destination, Ai::Steered>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [docks, &world](entt::registry & /*registry*/, double /*fixedStep*/) { docks->Step(world); }});
	return *docks;
}

std::string Docks::HullShape(std::string_view hull)
{
	return "hulls/" + std::string{hull};
}

std::string Docks::StationShape(std::string_view layout)
{
	return "stations/" + std::string{layout};
}

void Docks::DefineHull(std::string name, HullDefinition hull)
{
	CheckHull(name, hull);
	if (m_hulls.contains(name))
		throw std::invalid_argument{std::format("Dock: hull '{}' is already defined", name)};
	Physics::ShapeDefinition shape;
	shape.kind = Physics::ShapeKind::Box;
	shape.halfExtents = glm::vec3{hull.beam, hull.height, hull.length} * 0.5f;
	m_physics.DefineShape(HullShape(name), shape);
	m_hulls.emplace(std::move(name), std::move(hull));
}

void Docks::DefineLayout(std::string name, StationLayout layout)
{
	CheckLayout(name, layout);
	if (m_layouts.contains(name))
		throw std::invalid_argument{std::format("Dock: station layout '{}' is already defined", name)};
	m_physics.DefineShape(StationShape(name), DeckShape(layout));
	m_roads.emplace(name, Roads(layout));
	m_layouts.emplace(std::move(name), std::move(layout));
}

void Docks::Define(const Sim::DefinitionRegistry &definitions)
{
	for (const std::string &name : definitions.Names<HullDefinition>())
		DefineHull(name, *definitions.Find<HullDefinition>(name));
	for (const std::string &name : definitions.Names<StationLayout>())
		DefineLayout(name, *definitions.Find<StationLayout>(name));
}

const HullDefinition *Docks::FindHull(std::string_view name) const
{
	const auto found = m_hulls.find(name);
	return found == m_hulls.end() ? nullptr : &found->second;
}

const StationLayout *Docks::FindLayout(std::string_view name) const
{
	const auto found = m_layouts.find(name);
	return found == m_layouts.end() ? nullptr : &found->second;
}

const RoadGraph *Docks::FindRoads(std::string_view layout) const
{
	const auto found = m_roads.find(layout);
	return found == m_roads.end() ? nullptr : &found->second;
}

entt::entity Docks::MakeStation(
    Sim::SimWorld &world, std::uint64_t system, std::string_view body, std::string_view layoutName)
{
	const StationLayout *layout = FindLayout(layoutName);
	if (!layout)
		throw std::invalid_argument{
		    std::format("Dock: station '{}' is built to '{}', which is not defined", body, layoutName)};
	entt::registry &registry = world.Registry();
	const double now = world.Clock().Now();
	const Physics::OnRails rails{std::string{body}};
	// Placed first, so a system or a body physics does not have refuses before anything is made.
	const Sim::Transform at = m_physics.RailsPlace(system, rails, now);

	const entt::entity station = registry.create();
	registry.emplace<Sim::Transform>(station, at);
	registry.emplace<Sim::InSystem>(station, system);
	registry.emplace<Physics::OnRails>(station, rails);
	registry.emplace<Physics::LocalFrame>(station, std::string{body});
	registry.emplace<Physics::Body>(
	    station, Physics::Body{Physics::BodyKind::Kinematic, StationShape(layoutName)});
	Station built{std::string{layoutName}, {}, {}};
	for (std::size_t index = 0; index < layout->pads.size(); ++index)
	{
		const PadLayout &place = layout->pads[index];
		const Physics::OnRails padRails{std::string{body}, place.position, PadOrientation(place)};
		const entt::entity pad = registry.create();
		registry.emplace<Sim::Transform>(pad, m_physics.RailsPlace(system, padRails, now));
		registry.emplace<Sim::InSystem>(pad, system);
		registry.emplace<Physics::OnRails>(pad, padRails);
		registry.emplace<Physics::LocalFrame>(pad, std::string{body});
		Pad made;
		made.station = station;
		made.number = static_cast<std::uint32_t>(index + 1);
		made.padClass = place.padClass;
		made.display = place.display;
		registry.emplace<Pad>(pad, made);
		built.pads.push_back(pad);
	}
	registry.emplace<Station>(station, std::move(built));
	return station;
}

void Docks::Park(Sim::SimWorld &world, entt::entity ship, entt::entity padEntity)
{
	entt::registry &registry = world.Registry();
	const auto *pad = registry.valid(padEntity) ? registry.try_get<Pad>(padEntity) : nullptr;
	if (!pad || pad->state != PadState::Free || pad->display)
		throw std::invalid_argument{"Dock: a ship is parked on a free pad that is not for display"};
	const HullDefinition *hull = registry.valid(ship) ? HullOf(registry, ship) : nullptr;
	const auto *body = hull ? registry.try_get<Physics::Body>(ship) : nullptr;
	if (!body || body->kind != Physics::BodyKind::Dynamic || !Fits(*hull, *pad) ||
	    SystemOf(registry, ship) != SystemOf(registry, pad->station))
		throw std::invalid_argument{
		    "Dock: a ship parked is a dynamic body with a hull that fits the pad, in its system"};
	registry.emplace_or_replace<Docking>(ship, Docking{pad->station, padEntity, Stage::Docked, false});
	Pad &held = registry.get<Pad>(padEntity);
	held.state = PadState::Holding;
	held.ship = ship;
	Clamp(world, ship, world.Clock().Now());
}

void Docks::Request(Sim::SimWorld &world, const RequestPad &command)
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](std::string reason)
	{ world.Events().Emit(PadRefused{command.ship, command.station, std::move(reason)}); };
	const entt::entity ship = command.ship;
	if (!registry.valid(ship))
		return refuse("the ship is not in the world");
	if (!Sim::MayCommand(registry, ship, command.sender))
		return refuse("its sender does not control the ship");
	const entt::entity stationEntity = command.station;
	if (!registry.valid(stationEntity) || !registry.all_of<Station>(stationEntity))
		return refuse("it is not a station");
	const std::string_view name = NameOf(registry, stationEntity);
	const HullDefinition *hull = HullOf(registry, ship);
	if (!hull)
		return refuse("the ship has no hull the docks know");
	const auto *body = registry.try_get<Physics::Body>(ship);
	if (!body || body->kind != Physics::BodyKind::Dynamic ||
	    !registry
	         .all_of<Physics::Thrusters, Physics::FlightControls, Physics::Velocity, Sim::Transform>(
	             ship))
		return refuse("the ship is not one that flies");
	if (SystemOf(registry, ship) != SystemOf(registry, stationEntity))
		return refuse(std::format("the ship is not in {}'s system", name));
	if (FrameOf(registry, ship) != FrameOf(registry, stationEntity))
		return refuse(std::format("the ship is not in {}'s frame", name));
	if (const auto *existing = registry.try_get<Docking>(ship))
	{
		const std::string_view at = NameOf(registry, existing->station);
		if (existing->stage == Stage::Queued)
			return refuse(std::format("the ship is already waiting for a pad at {}", at));
		const auto *pad =
		    registry.valid(existing->pad) ? registry.try_get<Pad>(existing->pad) : nullptr;
		return refuse(std::format("the ship already holds pad {} at {}", pad ? pad->number : 0, at));
	}
	Station &station = registry.get<Station>(stationEntity);
	if (std::ranges::none_of(
	        station.pads, [&](entt::entity pad) { return Fits(*hull, registry.get<Pad>(pad)); }))
		return refuse(std::format("{} has no pad its hull fits", name));

	station.queue.push_back(ship);
	registry.emplace<Docking>(ship, Docking{stationEntity, entt::null, Stage::Queued, command.fly});
	Serve(world, stationEntity, world.Clock().Now());
	const std::vector<entt::entity> &queue = registry.get<Station>(stationEntity).queue;
	if (const auto place = std::ranges::find(queue, ship); place != queue.end())
		world.Events().Emit(
		    PadQueued{ship, stationEntity, static_cast<std::uint32_t>(place - queue.begin() + 1)});
}

void Docks::Release(Sim::SimWorld &world, const ReleasePad &command)
{
	entt::registry &registry = world.Registry();
	const entt::entity ship = command.ship;
	const auto *docking = registry.valid(ship) ? registry.try_get<Docking>(ship) : nullptr;
	const auto refuse = [&](std::string reason)
	{
		world.Events().Emit(
		    PadRefused{ship, docking ? docking->station : entt::entity{entt::null}, std::move(reason)});
	};
	if (!docking)
		return refuse("the ship holds no pad and waits for none");
	if (!Sim::MayCommand(registry, ship, command.sender))
		return refuse("its sender does not control the ship");
	switch (docking->stage)
	{
		case Stage::Queued:
		case Stage::Inbound:
		case Stage::Descending:
		{
			const entt::entity station = docking->station;
			GiveUp(world, ship, true);
			Serve(world, station, world.Clock().Now());
			return;
		}
		case Stage::Docked:
			if (const std::int64_t crates = ToCarry(registry, ship); crates > 0)
				return refuse(std::format("its cargo is still crossing the deck, {} crates", crates));
			Unclamp(world, ship);
			return;
		case Stage::Leaving: return refuse("the ship is already leaving its pad");
	}
}

void Docks::Step(Sim::SimWorld &world)
{
	JETTISON_PROFILE_ZONE("Dock::Docks::Step");
	entt::registry &registry = world.Registry();
	// Physics has placed everything where it is as the step ends.
	const double time = world.Clock().Now() + world.FixedStep();
	const std::uint64_t step = world.Clock().Step();

	// 1. What can no longer be held: a pad whose ship has gone or holds it no more, and a ship's
	// business with a station that has gone, or that it is no longer beside.
	for (const entt::entity padEntity : Sorted<Pad>(registry))
	{
		Pad &pad = registry.get<Pad>(padEntity);
		if (pad.state == PadState::Free)
			continue;
		const auto *docking = registry.valid(pad.ship) ? registry.try_get<Docking>(pad.ship) : nullptr;
		if (!docking || docking->pad != padEntity)
		{
			pad.state = PadState::Free;
			pad.ship = entt::null;
		}
	}
	for (const entt::entity ship : Sorted<Docking>(registry))
	{
		const Docking &docking = registry.get<Docking>(ship);
		const entt::entity station = docking.station;
		if (!registry.valid(station) || !registry.all_of<Station>(station) ||
		    SystemOf(registry, ship) != SystemOf(registry, station) ||
		    FrameOf(registry, ship) != FrameOf(registry, station))
			GiveUp(world, ship, true);
	}
	for (const entt::entity stationEntity : Sorted<Station>(registry))
		std::erase_if(
		    registry.get<Station>(stationEntity).queue,
		    [&](entt::entity ship)
		    {
			    const auto *docking = registry.valid(ship) ? registry.try_get<Docking>(ship) : nullptr;
			    return !docking || docking->station != stationEntity || docking->stage != Stage::Queued;
		    });

	// 2. Reservations that have lapsed.
	for (const entt::entity padEntity : Sorted<Pad>(registry))
	{
		const Pad &pad = registry.get<Pad>(padEntity);
		if (pad.state != PadState::Reserved || step < pad.lapses)
			continue;
		world.Events().Emit(PadLapsed{pad.ship, pad.station, padEntity, pad.number});
		GiveUp(world, pad.ship, false);
	}

	// 3. Each ship on: clamped once it is on its pad, down from above it, and clear of it once it
	// has lifted away.
	for (const entt::entity ship : Sorted<Docking>(registry))
	{
		Docking &docking = registry.get<Docking>(ship);
		const auto *destination = registry.try_get<Ai::Destination>(ship);
		switch (docking.stage)
		{
			case Stage::Queued:
			case Stage::Docked: break;
			case Stage::Inbound:
			case Stage::Descending:
			{
				const Pad &pad = registry.get<Pad>(docking.pad);
				if (InClampReach(registry, ship))
				{
					Clamp(world, ship, time);
					world.Events().Emit(Landed{ship, docking.station, docking.pad, pad.number});
				}
				else if (docking.fly && docking.stage == Stage::Inbound)
				{
					if (!destination)
						FlyInbound(world, ship, time);
					else if (destination->arrived)
					{
						docking.stage = Stage::Descending;
						FlyDown(world, ship);
					}
				}
				else if (docking.fly && !destination)
					FlyDown(world, ship);
				break;
			}
			case Stage::Leaving:
				if (!destination)
					FlyUp(world, ship);
				else if (destination->arrived)
					GiveUp(world, ship, true);
				break;
		}
	}

	// 4. The queues, from the front.
	for (const entt::entity station : Sorted<Station>(registry))
		Serve(world, station, time);

	// 5. The decks.
	WorkDecks(world, time);
}

entt::entity Docks::FreePadFor(
    const entt::registry &registry, const Station &station, entt::entity ship) const
{
	const HullDefinition *hull = HullOf(registry, ship);
	if (!hull)
		return entt::null;
	entt::entity best = entt::null;
	for (const entt::entity padEntity : station.pads)
	{
		const Pad &pad = registry.get<Pad>(padEntity);
		if (pad.state != PadState::Free || !Fits(*hull, pad))
			continue;
		// In number order, so the first of a class is the lowest numbered.
		if (best == entt::null || pad.padClass < registry.get<Pad>(best).padClass)
			best = padEntity;
	}
	return best;
}

void Docks::Serve(Sim::SimWorld &world, entt::entity stationEntity, double time)
{
	entt::registry &registry = world.Registry();
	std::vector<entt::entity> &queue = registry.get<Station>(stationEntity).queue;
	for (std::size_t index = 0; index < queue.size();)
	{
		const entt::entity pad =
		    FreePadFor(registry, registry.get<Station>(stationEntity), queue[index]);
		if (pad == entt::null)
		{
			++index;
			continue;
		}
		const entt::entity ship = queue[index];
		queue.erase(queue.begin() + static_cast<std::ptrdiff_t>(index));
		Give(world, ship, pad, time);
	}
}

void Docks::Give(Sim::SimWorld &world, entt::entity ship, entt::entity padEntity, double time)
{
	entt::registry &registry = world.Registry();
	Pad &pad = registry.get<Pad>(padEntity);
	const StationLayout &layout = m_layouts.at(registry.get<Station>(pad.station).layout);
	pad.state = PadState::Reserved;
	pad.ship = ship;
	pad.lapses =
	    world.Clock().Step() +
	    static_cast<std::uint64_t>(std::ceil(double{layout.reservationSeconds} / world.FixedStep()));
	Docking &docking = registry.get<Docking>(ship);
	docking.pad = padEntity;
	docking.stage = Stage::Inbound;
	world.Events().Emit(PadGiven{ship, pad.station, padEntity, pad.number});
	if (docking.fly)
		FlyInbound(world, ship, time);
}

void Docks::GiveUp(Sim::SimWorld &world, entt::entity ship, bool announce)
{
	entt::registry &registry = world.Registry();
	const Docking docking = registry.get<Docking>(ship);
	if (registry.valid(docking.station))
		if (auto *station = registry.try_get<Station>(docking.station))
			std::erase(station->queue, ship);
	if (registry.valid(docking.pad))
		if (auto *pad = registry.try_get<Pad>(docking.pad); pad && pad->ship == ship)
		{
			pad->state = PadState::Free;
			pad->ship = entt::null;
		}
	if (docking.stage == Stage::Docked)
	{
		// Sent away while clamped, along a lane: a ship again, wherever it is going.
		Physics::Body body = registry.get<Physics::Body>(ship);
		body.kind = Physics::BodyKind::Dynamic;
		registry.replace<Physics::Body>(ship, body);
		registry.remove<Physics::OnRails>(ship);
		registry.emplace_or_replace<Physics::Velocity>(ship);
	}
	if (docking.fly || docking.stage == Stage::Leaving)
		Hold(registry, ship);
	// Its buggies bring back whatever they were taking to it.
	registry.remove<Transfer>(ship);
	registry.remove<Docking>(ship);
	if (announce)
		world.Events().Emit(PadReleased{ship, docking.station});
}

void Docks::FlyInbound(Sim::SimWorld &world, entt::entity ship, double time)
{
	entt::registry &registry = world.Registry();
	const Docking &docking = registry.get<Docking>(ship);
	const Pad &pad = registry.get<Pad>(docking.pad);
	const Ai::Steered &steered = Steer(registry, ship);

	Ai::Destination destination;
	destination.entity = docking.pad;
	destination.offset = {0.0, AboveHeight(registry, ship, pad), 0.0};
	destination.arrivalRadius = kAboveRadius;
	destination.arrivalSpeed = kAboveSpeed;

	// Round the station's sphere to the point above the pad, in the ship's frame: the station turns
	// with its body, and the course is plotted again if the ship has to be flown in again.
	const std::uint64_t system = SystemOf(registry, ship);
	const glm::dvec3 origin = m_physics.FrameOrigin(system, FrameOf(registry, ship), time);
	const Sim::Transform &padAt = registry.get<Sim::Transform>(docking.pad);
	const glm::dvec3 above = padAt.position + padAt.orientation * destination.offset;
	const double radius = Radius(m_layouts.at(registry.get<Station>(pad.station).layout));
	const double clearance =
	    m_physics.ShapeRadius(registry.get<Physics::Body>(ship).shape) + double{steered.clearance};
	const Ai::Waypoints waypoints{
	    {Ai::Sphere{registry.get<Sim::Transform>(pad.station).position - origin, radius}}, clearance};
	if (const std::optional<Ai::Course> course =
	        waypoints.PlotCourse(registry.get<Sim::Transform>(ship).position - origin, above - origin))
		if (course->points.size() > 2)
			destination.waypoints.assign(course->points.begin() + 1, course->points.end() - 1);
	registry.emplace_or_replace<Ai::Destination>(ship, std::move(destination));
}

void Docks::FlyDown(Sim::SimWorld &world, entt::entity ship)
{
	entt::registry &registry = world.Registry();
	const Docking &docking = registry.get<Docking>(ship);
	Steer(registry, ship);
	Ai::Destination destination;
	destination.entity = docking.pad;
	destination.offset = {0.0, RestingHeight(*HullOf(registry, ship)) + kHover, 0.0};
	destination.byHull = true;
	destination.face = true;
	destination.speed = kDescentSpeed;
	destination.arrivalRadius = kLandingRadius;
	destination.arrivalSpeed = kLandingSpeed;
	registry.emplace_or_replace<Ai::Destination>(ship, std::move(destination));
}

void Docks::FlyUp(Sim::SimWorld &world, entt::entity ship)
{
	entt::registry &registry = world.Registry();
	const Docking &docking = registry.get<Docking>(ship);
	Steer(registry, ship);
	Ai::Destination destination;
	destination.entity = docking.pad;
	destination.offset = {0.0, AboveHeight(registry, ship, registry.get<Pad>(docking.pad)), 0.0};
	destination.byHull = true;
	destination.face = true;
	destination.speed = kLiftSpeed;
	destination.arrivalRadius = kAboveRadius;
	destination.arrivalSpeed = kAboveSpeed;
	registry.emplace_or_replace<Ai::Destination>(ship, std::move(destination));
}

bool Docks::InClampReach(const entt::registry &registry, entt::entity ship) const
{
	const Docking &docking = registry.get<Docking>(ship);
	const Sim::Transform &padAt = registry.get<Sim::Transform>(docking.pad);
	const Sim::Transform &shipAt = registry.get<Sim::Transform>(ship);
	const glm::dquat inverse = glm::conjugate(padAt.orientation);
	const glm::dvec3 offset = inverse * (shipAt.position - padAt.position) -
	                          glm::dvec3{0.0, RestingHeight(*HullOf(registry, ship)), 0.0};
	if (std::hypot(offset.x, offset.z) > kClampAcross || std::abs(offset.y) > kClampAbove)
		return false;
	const glm::dquat turned = inverse * shipAt.orientation;
	if (glm::dot(turned * glm::dvec3{0.0, 1.0, 0.0}, glm::dvec3{0.0, 1.0, 0.0}) < kClampUp ||
	    glm::dot(turned * glm::dvec3{0.0, 0.0, -1.0}, glm::dvec3{0.0, 0.0, -1.0}) < kClampNose)
		return false;
	const auto *velocity = registry.try_get<Physics::Velocity>(ship);
	return !velocity || glm::length(glm::dvec3{velocity->linear}) <= kClampSpeed;
}

void Docks::Clamp(Sim::SimWorld &world, entt::entity ship, double time)
{
	entt::registry &registry = world.Registry();
	Docking &docking = registry.get<Docking>(ship);
	Pad &pad = registry.get<Pad>(docking.pad);
	const PadLayout &place = PadOf(registry, pad);
	const Physics::OnRails &station = registry.get<Physics::OnRails>(pad.station);
	const glm::dquat turn = PadOrientation(place);
	const glm::dvec3 rest =
	    place.position + turn * glm::dvec3{0.0, RestingHeight(*HullOf(registry, ship)), 0.0};
	const Physics::OnRails rails{
	    station.body, station.offset + station.turn * rest, station.turn * turn};
	registry.emplace_or_replace<Physics::OnRails>(ship, rails);
	registry.replace<Sim::Transform>(ship, m_physics.RailsPlace(SystemOf(registry, ship), rails, time));
	Physics::Body body = registry.get<Physics::Body>(ship);
	body.kind = Physics::BodyKind::Kinematic;
	registry.replace<Physics::Body>(ship, body);
	registry.emplace_or_replace<Physics::Velocity>(ship);
	Hold(registry, ship);
	docking.stage = Stage::Docked;
	pad.state = PadState::Holding;
	pad.ship = ship;
}

void Docks::Unclamp(Sim::SimWorld &world, entt::entity ship)
{
	entt::registry &registry = world.Registry();
	Docking &docking = registry.get<Docking>(ship);
	Physics::Body body = registry.get<Physics::Body>(ship);
	body.kind = Physics::BodyKind::Dynamic;
	registry.replace<Physics::Body>(ship, body);
	registry.remove<Physics::OnRails>(ship);
	registry.emplace_or_replace<Physics::Velocity>(ship);
	docking.stage = Stage::Leaving;
	world.Events().Emit(
	    Left{ship, docking.station, docking.pad, registry.get<Pad>(docking.pad).number});
	FlyUp(world, ship);
}

const PadLayout &Docks::PadOf(const entt::registry &registry, const Pad &pad) const
{
	return m_layouts.at(registry.get<Station>(pad.station).layout).pads.at(pad.number - 1);
}

const HullDefinition *Docks::HullOf(const entt::registry &registry, entt::entity ship) const
{
	const auto *hull = registry.try_get<Hull>(ship);
	return hull ? FindHull(hull->definition) : nullptr;
}

double Docks::AboveHeight(const entt::registry &registry, entt::entity ship, const Pad &pad) const
{
	const PadLayout &place = PadOf(registry, pad);
	const auto *steered = registry.try_get<Ai::Steered>(ship);
	const double reach = Radius(m_layouts.at(registry.get<Station>(pad.station).layout)) +
	                     m_physics.ShapeRadius(registry.get<Physics::Body>(ship).shape) +
	                     double{steered ? steered->clearance : Ai::Steered{}.clearance} + kAboveMargin;
	// Where the line out along the normal from the pad crosses that sphere: |p + n h| = reach.
	const double along = glm::dot(place.position, place.normal);
	return -along + std::sqrt(along * along - glm::dot(place.position, place.position) + reach * reach);
}

double Docks::RestingHeight(const HullDefinition &hull)
{
	return double{hull.height} * 0.5;
}
} // namespace Jettison::Dock
