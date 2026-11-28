#include <jettison/dock/Docks.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include <jettison/ai/Courses.h>
#include <jettison/ai/Steering.h>
#include <jettison/core/Log.h>
#include <jettison/physics/CollisionFile.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/profile/Profile.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Sorted.h"

namespace Jettison::Dock
{
namespace
{
// A clamp's reach: how far across the berth from where the vehicle rests, and above or below it, a
// vehicle can be and be clamped, how far it can be turned from the berth, and how fast it can be
// moving. Clamping puts it where it rests, so these are what it may be moved by.
constexpr double kClampAcross = 2.0;
constexpr double kClampAbove = 1.0;
constexpr double kClampSpeed = 0.5;
const double kClampUp = std::cos(glm::radians(10.0));
const double kClampNose = std::cos(glm::radians(15.0));

// How far above its resting height a vehicle flown down aims to stop, a hand's breadth: the clamp
// takes it from there, and Jolt's contacts begin a couple of centimetres before two bodies meet, so
// a vehicle flown onto the deck itself would touch it first.
constexpr double kHover = 0.25;

// The share of the hover within which a course's end is resting on a deck (Docks::CourseAmong).
constexpr double kRestingShare = 0.5;

// Beyond the platform's sphere, the vehicle's shape and its clearance, the point above a berth is
// this far again, so a vehicle arriving there is outside what steering keeps it from, and not at its
// edge.
constexpr double kAboveMargin = 10.0;

// How a vehicle is flown through the stages: the point above its berth it arrives at, the speed it
// comes down and lifts at, and how near it stops above the berth.
constexpr float kAboveRadius = 20.0f;
constexpr float kAboveSpeed = 3.0f;
constexpr float kDescentSpeed = 40.0f;
constexpr float kLiftSpeed = 20.0f;
constexpr float kLandingRadius = 0.25f;
constexpr float kLandingSpeed = 0.2f;

// A covered berth's way in (docs/docks.md, "A covered berth"): how far up its headroom the point over
// it is, how near that point counts as reached, and how far beyond the platform's sphere, as a share
// of its diameter, its ways out from under the cover reach.
constexpr double kCoveredShare = 0.5;
constexpr float kCoveredRadius = 2.0f;

// The speed among them, and how near a waypoint counts as passed: a course among decks turns
// within metres of them, so a corner is taken slowly and close, not at speed 40 m wide of it.
constexpr float kCoveredSpeed = 20.0f;
constexpr float kCoveredPass = 5.0f;
constexpr double kWayOutReach = 1.0;

constexpr Core::Category kDock{"dock"};

// How far past the point above a berth its corridor's mouth is, as a share of the platform's radius:
// room out along the normal for a vehicle that came round the sphere to turn down the corridor.
constexpr double kMouthShare = 1.0;

using Detail::Sorted;


std::string_view LocalFrameOf(const entt::registry &registry, entt::entity entity)
{
	const auto *frame = registry.try_get<Physics::LocalFrame>(entity);
	return frame ? std::string_view{frame->body} : std::string_view{};
}


std::uint64_t FrameOf(const entt::registry &registry, entt::entity entity)
{
	const auto *inFrame = registry.try_get<Sim::InFrame>(entity);
	return inFrame ? inFrame->frame : Sim::kNoFrame;
}


// The vehicle's steering, as it keeps clear, given it by default if it has none: the docks fly it.
const Ai::Steered &Steer(entt::registry &registry, entt::entity vehicle)
{
	if (const auto *steered = registry.try_get<Ai::Steered>(vehicle))
		return *steered;
	return registry.emplace<Ai::Steered>(vehicle);
}


void Hold(entt::registry &registry, entt::entity vehicle)
{
	registry.remove<Ai::Destination>(vehicle);
	if (auto *controls = registry.try_get<Physics::FlightControls>(vehicle))
		*controls = Physics::FlightControls{};
}
} // namespace


bool IsDocked(const entt::registry &registry, entt::entity vehicle, entt::entity platform)
{
	const auto *docking = registry.valid(vehicle) ? registry.try_get<Docking>(vehicle) : nullptr;
	return docking && docking->stage == Stage::Docked && docking->platform == platform;
}


Docks &Docks::Attach(Sim::SimWorld &world, Physics::PhysicsWorld &physics)
{
	RegisterTypes();

	// Shared by the commands and the system, which the world keeps for as long as it lives.
	std::shared_ptr<Docks> docks{new Docks{physics}};
	world.RegisterComponent<Vehicle>();
	world.RegisterComponent<Platform>();
	world.RegisterComponent<Berth>();
	world.RegisterComponent<Docking>();

	// A berth's state is what its lamps show, a vehicle's stage and kind whether and how its berth's
	// clamps close on it, and presentation reads snapshots.
	world.ShowInSnapshots<Berth>();
	world.ShowInSnapshots<Docking>();
	world.ShowInSnapshots<Vehicle>();
	world.RegisterCommand<RequestBerth>([docks](Sim::SimWorld &target, const RequestBerth &command)
	                                    { docks->Request(target, command); });
	world.RegisterCommand<ReleaseBerth>([docks](Sim::SimWorld &target, const ReleaseBerth &command)
	                                    { docks->Release(target, command); });
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Vehicle, Physics::LocalFrame, Physics::Handling>()
	         .Writing<
	             Platform, Berth, Docking, Physics::Body, Physics::OnRails, Physics::Velocity,
	             Physics::FlightControls, Sim::Transform, Sim::InFrame, Ai::Destination, Ai::Steered>(),
	     {std::string{Physics::PhysicsWorld::kSystemName}},
	     [docks, &world](entt::registry & /*registry*/, double /*fixedStep*/) { docks->Step(world); }},
	    {.next = [docks](const Sim::SimWorld &target) { return docks->QuietFrom(target); }});
	return *docks;
}


std::string Docks::VehicleShape(std::string_view vehicle)
{
	return "vehicles/" + std::string{vehicle};
}


std::string Docks::PlatformShape(std::string_view layout)
{
	return "platforms/" + std::string{layout};
}


void Docks::DefineBerthClass(std::string name, BerthClassDefinition berthClass)
{
	CheckBerthClass(name, berthClass);
	if (m_berthClasses.contains(name))
		throw std::invalid_argument{std::format("Dock: berth class '{}' is already defined", name)};
	m_berthClasses.emplace(std::move(name), berthClass);
}


void Docks::DefineVehicle(std::string name, VehicleDefinition vehicle, const Vfs::FileSystem *pFiles)
{
	CheckVehicle(name, vehicle, m_berthClasses);
	if (m_vehicles.contains(name))
		throw std::invalid_argument{std::format("Dock: vehicle '{}' is already defined", name)};
	Physics::ShapeDefinition shape;
	if (vehicle.collision.empty())
	{
		shape.kind = Physics::ShapeKind::Box;
		shape.halfExtents = glm::vec3{vehicle.beam, vehicle.height, vehicle.length} * 0.5f;
	}
	else
	{
		if (!pFiles)
			throw std::invalid_argument{std::format(
			    "Dock: vehicle '{}' names the collision file '{}', and no files are given to read it",
			    name, vehicle.collision)};
		shape = Physics::ReadCollisionFile(*pFiles, vehicle.collision);
		if (std::ranges::any_of(
		        shape.parts, [](const Physics::ShapeDefinition &part)
		        { return part.kind == Physics::ShapeKind::Mesh; }))
			throw Serialise::FormatError{std::format(
			    "Dock: vehicle '{}' names the collision file '{}', which holds a mesh: a vehicle flies "
			    "as a dynamic body, and a mesh has no inside",
			    name, vehicle.collision)};
	}
	m_physics.DefineShape(VehicleShape(name), shape);
	m_clampGrips.emplace(name, GripsOf(name, vehicle));
	m_vehicles.emplace(std::move(name), std::move(vehicle));
}


void Docks::DefineLayout(std::string name, PlatformLayout layout)
{
	CheckLayout(name, layout, m_berthClasses);
	if (m_layouts.contains(name))
		throw std::invalid_argument{std::format("Dock: platform layout '{}' is already defined", name)};
	m_physics.DefineShape(PlatformShape(name), DeckShape(layout));
	m_layouts.emplace(std::move(name), std::move(layout));
}


void Docks::Define(const Sim::DefinitionRegistry &definitions)
{
	for (const std::string &name : definitions.Names<BerthClassDefinition>())
		DefineBerthClass(name, *definitions.Find<BerthClassDefinition>(name));
	for (const std::string &name : definitions.Names<VehicleDefinition>())
		DefineVehicle(name, *definitions.Find<VehicleDefinition>(name), &definitions.Files());
	for (const std::string &name : definitions.Names<PlatformLayout>())
		DefineLayout(name, *definitions.Find<PlatformLayout>(name));
}


const BerthClassDefinition *Docks::FindBerthClass(std::string_view name) const
{
	const auto found = m_berthClasses.find(name);
	return found == m_berthClasses.end() ? nullptr : &found->second;
}


bool Docks::Fits(const VehicleDefinition &vehicle, std::string_view berthClass) const
{
	return Dock::Fits(m_berthClasses, vehicle, berthClass);
}


bool Docks::Offers(
    const entt::registry &registry, entt::entity berthEntity, std::string_view feature) const
{
	const auto *berth = registry.valid(berthEntity) ? registry.try_get<Berth>(berthEntity) : nullptr;
	const auto *platform = berth && registry.valid(berth->platform)
	                           ? registry.try_get<Platform>(berth->platform)
	                           : nullptr;
	const PlatformLayout *layout = platform ? FindLayout(platform->layout) : nullptr;
	if (!layout || berth->number == 0 || berth->number > layout->berths.size())
		return false;
	const std::vector<std::string> &features = layout->berths[berth->number - 1].features;
	return std::ranges::find(features, feature) != features.end();
}


const VehicleDefinition *Docks::FindVehicle(std::string_view name) const
{
	const auto found = m_vehicles.find(name);
	return found == m_vehicles.end() ? nullptr : &found->second;
}


const ClampGrips *Docks::FindClampGrips(std::string_view vehicle) const
{
	const auto found = m_clampGrips.find(vehicle);
	return found == m_clampGrips.end() ? nullptr : &found->second;
}


const PlatformLayout *Docks::FindLayout(std::string_view name) const
{
	const auto found = m_layouts.find(name);
	return found == m_layouts.end() ? nullptr : &found->second;
}


entt::entity Docks::MakePlatform(
    Sim::SimWorld &world, std::uint64_t frame, std::string_view body, std::string_view layoutName)
{
	const PlatformLayout *layout = FindLayout(layoutName);
	if (!layout)
		throw std::invalid_argument{std::format(
		    "Dock: platform '{}' is built to '{}', which is not defined", body, layoutName)};
	entt::registry &registry = world.Registry();
	const double now = world.Clock().Now();
	const Physics::OnRails rails{std::string{body}};

	// Placed first, so a frame or a body physics does not have refuses before anything is made.
	const Sim::Transform at = m_physics.RailsPlace(frame, rails, now);

	const entt::entity platform = registry.create();
	registry.emplace<Sim::Transform>(platform, at);
	registry.emplace<Sim::InFrame>(platform, frame);
	registry.emplace<Physics::OnRails>(platform, rails);
	registry.emplace<Physics::LocalFrame>(platform, std::string{body});
	registry.emplace<Physics::Body>(
	    platform, Physics::Body{Physics::BodyKind::Kinematic, PlatformShape(layoutName)});
	Platform built{std::string{layoutName}, {}, {}};
	for (std::size_t index = 0; index < layout->berths.size(); ++index)
	{
		const BerthLayout &place = layout->berths[index];
		const Physics::OnRails berthRails{std::string{body}, place.position, BerthOrientation(place)};
		const entt::entity berth = registry.create();
		registry.emplace<Sim::Transform>(berth, m_physics.RailsPlace(frame, berthRails, now));
		registry.emplace<Sim::InFrame>(berth, frame);
		registry.emplace<Physics::OnRails>(berth, berthRails);
		registry.emplace<Physics::LocalFrame>(berth, std::string{body});
		Berth made;
		made.platform = platform;
		made.number = static_cast<std::uint32_t>(index + 1);
		made.berthClass = place.berthClass;
		made.display = place.display;
		registry.emplace<Berth>(berth, made);
		built.berths.push_back(berth);
	}
	registry.emplace<Platform>(platform, std::move(built));
	return platform;
}


void Docks::Park(Sim::SimWorld &world, entt::entity vehicle, entt::entity berthEntity)
{
	entt::registry &registry = world.Registry();
	const auto *berth = registry.valid(berthEntity) ? registry.try_get<Berth>(berthEntity) : nullptr;
	if (!berth || berth->state != BerthState::Free || berth->display)
		throw std::invalid_argument{
		    "Dock: a vehicle is parked on a free berth that is not for display"};
	const VehicleDefinition *definition =
	    registry.valid(vehicle) ? VehicleOf(registry, vehicle) : nullptr;
	const auto *body = definition ? registry.try_get<Physics::Body>(vehicle) : nullptr;
	if (!body || body->kind != Physics::BodyKind::Dynamic || berth->display ||
	    !Fits(*definition, berth->berthClass) ||
	    FrameOf(registry, vehicle) != FrameOf(registry, berth->platform))
		throw std::invalid_argument{
		    "Dock: a vehicle parked is a dynamic body of a kind that fits the berth, in its frame"};
	registry.emplace_or_replace<Docking>(
	    vehicle, Docking{berth->platform, berthEntity, Stage::Docked, false});
	Berth &held = registry.get<Berth>(berthEntity);
	held.state = BerthState::Holding;
	held.vehicle = vehicle;
	Clamp(world, vehicle, world.Clock().Now());
}


void Docks::SetControl(Control control)
{
	m_control = std::move(control);
}


void Docks::Request(Sim::SimWorld &world, const RequestBerth &command)
{
	entt::registry &registry = world.Registry();
	const auto refuse = [&](Reason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{
		world.Events().Emit(
		    BerthRefused{
		        command.vehicle, command.platform, Sim::MakeReason(reason, std::move(arguments))});
	};
	const entt::entity vehicle = command.vehicle;
	if (!registry.valid(vehicle))
		return refuse(Reason::VehicleGone);
	if (!Sim::MayCommand(registry, vehicle, command.sender))
		return refuse(Reason::NotController);
	const entt::entity platformEntity = command.platform;
	if (!registry.valid(platformEntity) || !registry.all_of<Platform>(platformEntity))
		return refuse(Reason::NotAPlatform);
	const VehicleDefinition *definition = VehicleOf(registry, vehicle);
	if (!definition)
		return refuse(Reason::NoVehicle);
	const auto *body = registry.try_get<Physics::Body>(vehicle);
	if (!body || body->kind != Physics::BodyKind::Dynamic ||
	    !registry.all_of<Physics::Handling, Physics::FlightControls, Physics::Velocity, Sim::Transform>(
	        vehicle))
		return refuse(Reason::CannotFly);
	if (FrameOf(registry, vehicle) != FrameOf(registry, platformEntity))
		return refuse(Reason::OtherFrame, {Sim::ReasonArgument::Entity(platformEntity)});
	if (LocalFrameOf(registry, vehicle) != LocalFrameOf(registry, platformEntity))
		return refuse(Reason::OtherLocalFrame, {Sim::ReasonArgument::Entity(platformEntity)});
	if (const auto *existing = registry.try_get<Docking>(vehicle))
	{
		const Sim::ReasonArgument at = Sim::ReasonArgument::Entity(existing->platform);
		if (existing->stage == Stage::Queued)
			return refuse(Reason::AlreadyQueued, {at});
		const auto *berth =
		    registry.valid(existing->berth) ? registry.try_get<Berth>(existing->berth) : nullptr;
		return refuse(
		    Reason::AlreadyHoldsBerth, {Sim::ReasonArgument::Integer(berth ? berth->number : 0), at});
	}
	Platform &platform = registry.get<Platform>(platformEntity);
	if (std::ranges::none_of(
	        platform.berths,
	        [&](entt::entity berth)
	        {
		        const Berth &candidate = registry.get<Berth>(berth);
		        return !candidate.display && Fits(*definition, candidate.berthClass);
	        }))
		return refuse(Reason::NoBerthFits, {Sim::ReasonArgument::Entity(platformEntity)});
	if (!command.feature.empty() && std::ranges::none_of(
	                                    platform.berths,
	                                    [&](entt::entity berth)
	                                    {
		                                    const Berth &candidate = registry.get<Berth>(berth);
		                                    return !candidate.display &&
		                                           Fits(*definition, candidate.berthClass) &&
		                                           Offers(registry, berth, command.feature);
	                                    }))
		return refuse(Reason::NoBerthOffers, {Sim::ReasonArgument::Entity(platformEntity)});
	if (m_control.admit)
		if (std::optional<Sim::Reason> refused = m_control.admit(world, command))
			return world.Events().Emit(BerthRefused{vehicle, platformEntity, std::move(*refused)});

	platform.queue.push_back(vehicle);
	registry.emplace<Docking>(
	    vehicle, Docking{platformEntity, entt::null, Stage::Queued, command.fly, command.feature});
	Serve(world, platformEntity, world.Clock().Now());
	const std::vector<entt::entity> queue = Ranked(world, platformEntity);
	if (const auto place = std::ranges::find(queue, vehicle); place != queue.end())
		world.Events().Emit(
		    BerthQueued{
		        vehicle, platformEntity, static_cast<std::uint32_t>(place - queue.begin() + 1)});
}


void Docks::Release(Sim::SimWorld &world, const ReleaseBerth &command)
{
	entt::registry &registry = world.Registry();
	const entt::entity vehicle = command.vehicle;
	auto *docking = registry.valid(vehicle) ? registry.try_get<Docking>(vehicle) : nullptr;
	const auto refuse = [&](Reason reason, std::vector<Sim::ReasonArgument> arguments = {})
	{
		world.Events().Emit(
		    BerthRefused{
		        vehicle, docking ? docking->platform : entt::entity{entt::null},
		        Sim::MakeReason(reason, std::move(arguments))});
	};
	if (!docking)
		return refuse(Reason::HoldsNothing);
	if (!Sim::MayCommand(registry, vehicle, command.sender))
		return refuse(Reason::NotController);
	switch (docking->stage)
	{
		case Stage::Queued:
		case Stage::Inbound:
		case Stage::Descending:
		{
			const entt::entity platform = docking->platform;
			GiveUp(world, vehicle, true);
			Serve(world, platform, world.Clock().Now());
			return;
		}
		case Stage::Docked:
			docking->fly = command.fly;
			Unclamp(world, vehicle);
			return;
		case Stage::Leaving: return refuse(Reason::AlreadyLeaving);
	}
}


void Docks::Step(Sim::SimWorld &world)
{
	JETTISON_PROFILE_ZONE("Dock::Docks::Step");
	entt::registry &registry = world.Registry();

	// Physics has placed everything where it is as the step ends.
	const double time = world.Clock().Now() + world.FixedStep();
	const std::uint64_t step = world.Clock().Step();

	// 1. What can no longer be held: a berth whose vehicle has gone or holds it no more, and a
	// vehicle's business with a platform that has gone, or that it is no longer beside.
	for (const entt::entity berthEntity : Sorted<Berth>(registry))
	{
		Berth &berth = registry.get<Berth>(berthEntity);
		if (berth.state == BerthState::Free)
			continue;
		const auto *docking =
		    registry.valid(berth.vehicle) ? registry.try_get<Docking>(berth.vehicle) : nullptr;
		if (!docking || docking->berth != berthEntity)
		{
			berth.state = BerthState::Free;
			berth.vehicle = entt::null;
		}
	}
	for (const entt::entity vehicle : Sorted<Docking>(registry))
	{
		const Docking &docking = registry.get<Docking>(vehicle);
		const entt::entity platform = docking.platform;
		if (!registry.valid(platform) || !registry.all_of<Platform>(platform) ||
		    FrameOf(registry, vehicle) != FrameOf(registry, platform) ||
		    LocalFrameOf(registry, vehicle) != LocalFrameOf(registry, platform))
			GiveUp(world, vehicle, true);
	}
	for (const entt::entity platformEntity : Sorted<Platform>(registry))
		std::erase_if(
		    registry.get<Platform>(platformEntity).queue,
		    [&](entt::entity vehicle)
		    {
			    const auto *docking =
			        registry.valid(vehicle) ? registry.try_get<Docking>(vehicle) : nullptr;
			    return !docking || docking->platform != platformEntity ||
			           docking->stage != Stage::Queued;
		    });

	// 2. Reservations that have lapsed.
	for (const entt::entity berthEntity : Sorted<Berth>(registry))
	{
		Berth &berth = registry.get<Berth>(berthEntity);
		if (berth.state != BerthState::Reserved)
			continue;

		// Put off while the vehicle comes nearer: a reservation is lost by a vehicle that has stopped
		// making progress, not by one still on its way.
		const double distance = glm::distance(
		    registry.get<Sim::Transform>(berth.vehicle).position,
		    registry.get<Sim::Transform>(berthEntity).position);
		if (distance < berth.nearest - kProgress)
		{
			berth.nearest = distance;
			berth.lapses =
			    step +
			    ReservationSteps(world, m_layouts.at(registry.get<Platform>(berth.platform).layout));
		}
		if (step < berth.lapses)
			continue;
		world.Events().Emit(BerthLapsed{berth.vehicle, berth.platform, berthEntity, berth.number});
		GiveUp(world, berth.vehicle, false);
	}

	// 3. Each vehicle on: clamped once it is on its berth, down from above it, and clear of it once it
	// has lifted away.
	for (const entt::entity vehicle : Sorted<Docking>(registry))
	{
		Docking &docking = registry.get<Docking>(vehicle);
		const auto *destination = registry.try_get<Ai::Destination>(vehicle);
		switch (docking.stage)
		{
			case Stage::Queued:
			case Stage::Docked: break;
			case Stage::Inbound:
			case Stage::Descending:
			{
				const Berth &berth = registry.get<Berth>(docking.berth);
				if (InClampReach(registry, vehicle))
				{
					Clamp(world, vehicle, time);
					world.Events().Emit(Landed{vehicle, docking.platform, docking.berth, berth.number});
				}
				else if (docking.fly && docking.stage == Stage::Inbound)
				{
					if (!destination)
						FlyInbound(world, vehicle, time);

					// Into the corridor once past the course round the sphere; under a cover, once
					// over the berth, then flown down onto it.
					else if (
					    destination->corridor && destination->next >= destination->waypoints.size())
						docking.stage = Stage::Descending;
					else if (!destination->corridor && destination->arrived)
					{
						docking.stage = Stage::Descending;
						FlyDown(world, vehicle);
					}
				}
				else if (docking.fly && !destination)
					FlyDown(world, vehicle);
				break;
			}
			case Stage::Leaving:
				if (!docking.fly)
				{
					// Lifted again each step, as a SetFlightControls between would leave it.
					if (Lifted(registry, vehicle))
						GiveUp(world, vehicle, true);
					else
						Lift(registry, vehicle);
				}
				else if (!destination)
					FlyUp(world, vehicle);
				else if (destination->arrived)
					GiveUp(world, vehicle, true);
				break;
		}
	}

	// 4. The queues, from the front.
	for (const entt::entity platform : Sorted<Platform>(registry))
		Serve(world, platform, time);
}


std::uint64_t Docks::QuietFrom(const Sim::SimWorld &world) const
{
	const entt::registry &registry = world.Registry();
	const std::uint64_t now = world.Clock().Step();

	// 1 and 3: what can no longer be held, and every vehicle on its way.
	for (const auto [berthEntity, berth] : registry.view<const Berth>().each())
	{
		if (berth.state == BerthState::Free)
			continue;
		const auto *docking =
		    registry.valid(berth.vehicle) ? registry.try_get<Docking>(berth.vehicle) : nullptr;
		if (!docking || docking->berth != berthEntity)
			return now;
	}
	for (const auto [vehicle, docking] : registry.view<const Docking>().each())
	{
		const entt::entity platform = docking.platform;
		if ((docking.stage != Stage::Queued && docking.stage != Stage::Docked) ||
		    !registry.valid(platform) || !registry.all_of<Platform>(platform) ||
		    FrameOf(registry, vehicle) != FrameOf(registry, platform) ||
		    LocalFrameOf(registry, vehicle) != LocalFrameOf(registry, platform))
			return now;
	}

	// 4, and 1's queues: a vehicle queued that is not, or that a berth is free for.
	for (const auto [platformEntity, platform] : registry.view<const Platform>().each())
		for (const entt::entity vehicle : platform.queue)
		{
			const auto *docking =
			    registry.valid(vehicle) ? registry.try_get<Docking>(vehicle) : nullptr;
			if (!docking || docking->platform != platformEntity || docking->stage != Stage::Queued ||
			    FreeBerthFor(registry, platform, vehicle) != entt::null)
				return now;
		}

	// 2: a reservation lapses in the step its count begins.
	std::uint64_t next = std::numeric_limits<std::uint64_t>::max();
	for (const auto [berthEntity, berth] : registry.view<const Berth>().each())
		if (berth.state == BerthState::Reserved)
			next = std::min(next, berth.lapses);
	return next;
}


entt::entity Docks::FreeBerthFor(
    const entt::registry &registry, const Platform &platform, entt::entity vehicle) const
{
	const VehicleDefinition *definition = VehicleOf(registry, vehicle);
	if (!definition)
		return entt::null;
	const auto *docking = registry.try_get<Docking>(vehicle);
	const std::string_view feature = docking ? std::string_view{docking->feature} : std::string_view{};
	entt::entity best = entt::null;
	for (const entt::entity berthEntity : platform.berths)
	{
		const Berth &berth = registry.get<Berth>(berthEntity);
		if (berth.state != BerthState::Free || berth.display || !Fits(*definition, berth.berthClass) ||
		    (!feature.empty() && !Offers(registry, berthEntity, feature)))
			continue;

		// In number order, so the first of a class is the lowest numbered.
		if (best == entt::null || m_berthClasses.at(berth.berthClass).rank <
		                              m_berthClasses.at(registry.get<Berth>(best).berthClass).rank)
			best = berthEntity;
	}
	return best;
}


void Docks::Serve(Sim::SimWorld &world, entt::entity platformEntity, double time)
{
	entt::registry &registry = world.Registry();
	for (const entt::entity vehicle : Ranked(world, platformEntity))
	{
		const entt::entity berth =
		    FreeBerthFor(registry, registry.get<Platform>(platformEntity), vehicle);
		if (berth == entt::null)
			continue;
		std::erase(registry.get<Platform>(platformEntity).queue, vehicle);
		Give(world, vehicle, berth, time);
	}
}


std::vector<entt::entity> Docks::Ranked(const Sim::SimWorld &world, entt::entity platform) const
{
	std::vector<entt::entity> queue = world.Registry().get<Platform>(platform).queue;
	if (!m_control.rank)
		return queue;

	// Asked once a vehicle, not in a comparison, so a rule that answers differently twice cannot break
	// the sort.
	std::vector<std::pair<std::int64_t, entt::entity>> ranked;
	ranked.reserve(queue.size());
	for (const entt::entity vehicle : queue)
		ranked.emplace_back(m_control.rank(world, platform, vehicle), vehicle);
	std::ranges::stable_sort(
	    ranked, std::ranges::greater{}, [](const auto &entry) { return entry.first; });
	std::ranges::transform(ranked, queue.begin(), [](const auto &entry) { return entry.second; });
	return queue;
}


void Docks::Give(Sim::SimWorld &world, entt::entity vehicle, entt::entity berthEntity, double time)
{
	entt::registry &registry = world.Registry();
	Berth &berth = registry.get<Berth>(berthEntity);
	const PlatformLayout &layout = m_layouts.at(registry.get<Platform>(berth.platform).layout);
	berth.state = BerthState::Reserved;
	berth.vehicle = vehicle;
	berth.lapses = world.Clock().Step() + ReservationSteps(world, layout);
	berth.nearest = glm::distance(
	    registry.get<Sim::Transform>(vehicle).position,
	    registry.get<Sim::Transform>(berthEntity).position);
	Docking &docking = registry.get<Docking>(vehicle);
	docking.berth = berthEntity;
	docking.stage = Stage::Inbound;
	world.Events().Emit(BerthGiven{vehicle, berth.platform, berthEntity, berth.number});
	if (docking.fly)
		FlyInbound(world, vehicle, time);
}


void Docks::GiveUp(Sim::SimWorld &world, entt::entity vehicle, bool announce)
{
	entt::registry &registry = world.Registry();
	const Docking docking = registry.get<Docking>(vehicle);
	if (registry.valid(docking.platform))
		if (auto *platform = registry.try_get<Platform>(docking.platform))
			std::erase(platform->queue, vehicle);
	if (registry.valid(docking.berth))
		if (auto *berth = registry.try_get<Berth>(docking.berth); berth && berth->vehicle == vehicle)
		{
			berth->state = BerthState::Free;
			berth->vehicle = entt::null;
		}
	if (docking.stage == Stage::Docked)
	{
		// Sent away while clamped, through a portal: a vehicle again, wherever it is going.
		Physics::Body body = registry.get<Physics::Body>(vehicle);
		body.kind = Physics::BodyKind::Dynamic;
		registry.replace<Physics::Body>(vehicle, body);
		registry.remove<Physics::OnRails>(vehicle);
		registry.emplace_or_replace<Physics::Velocity>(vehicle);
	}
	if (docking.fly || docking.stage == Stage::Leaving)
		Hold(registry, vehicle);
	registry.remove<Docking>(vehicle);
	if (announce)
		world.Events().Emit(BerthReleased{vehicle, docking.platform});
}


void Docks::FlyInbound(Sim::SimWorld &world, entt::entity vehicle, double time)
{
	entt::registry &registry = world.Registry();
	const Docking &docking = registry.get<Docking>(vehicle);
	const Berth &berth = registry.get<Berth>(docking.berth);
	const Ai::Steered &steered = Steer(registry, vehicle);

	// Under a cover, among the decks and piers to the point over the berth, in the berth's axes so
	// the course turns with the platform, and kept clear of the structure by the vehicle's shape.
	if (const std::optional<double> covered = CoveredHeight(registry, vehicle, berth))
	{
		const Sim::Transform &berthAt = registry.get<Sim::Transform>(docking.berth);
		Ai::Destination destination;
		destination.entity = docking.berth;
		destination.offset = {0.0, *covered, 0.0};
		destination.waypointsOnEntity = true;
		destination.byShape = true;
		destination.speed = kCoveredSpeed;
		destination.passRadius = kCoveredPass;
		destination.arrivalRadius = kCoveredRadius;
		destination.arrivalSpeed = kAboveSpeed;
		const glm::dvec3 over = berthAt.position + berthAt.orientation * destination.offset;
		if (const std::optional<Ai::Course> course = CourseAmong(
		        registry, vehicle, berth, registry.get<Sim::Transform>(vehicle).position, over))
		{
			const glm::dquat inverse = glm::conjugate(berthAt.orientation);
			for (std::size_t point = 1; point + 1 < course->points.size(); ++point)
				destination.waypoints.push_back(inverse * (course->points[point] - berthAt.position));
		}
		else
			Core::Log(
			    Core::Severity::Warning, kDock,
			    "No way in among the decks and piers to berth {} was found; it is flown straight to",
			    berth.number);
		registry.emplace_or_replace<Ai::Destination>(vehicle, std::move(destination));
		return;
	}

	// Round the platform's sphere to the corridor's mouth, out along the berth's normal past the point
	// above it by the platform's radius again, and down the corridor onto the berth without stopping:
	// the course at a speed that falls with the way left, the corridor flown along its line, the
	// velocity across it damped (Ai::Destination::corridor), and the platform not an obstacle on it,
	// as it is not on the way down (byShape). In the berth's axes, so it turns with the platform; it
	// is plotted again if the vehicle has to be flown in again.
	const PlatformLayout &layout = m_layouts.at(registry.get<Platform>(berth.platform).layout);
	const double radius = Radius(layout);
	const Sim::Transform &berthAt = registry.get<Sim::Transform>(docking.berth);
	const glm::dvec3 mouth = {0.0, AboveHeight(registry, vehicle, berth) + radius * kMouthShare, 0.0};
	Ai::Destination destination;
	destination.entity = docking.berth;
	destination.offset = {0.0, RestingHeight(*VehicleOf(registry, vehicle)) + kHover, 0.0};
	destination.waypointsOnEntity = true;
	destination.corridor = true;
	destination.byShape = true;
	destination.face = true;
	destination.arrivalRadius = kLandingRadius;
	destination.arrivalSpeed = kLandingSpeed;
	const double clearance =
	    m_physics.ShapeRadius(registry.get<Physics::Body>(vehicle).shape) + double{steered.clearance};
	const Ai::Waypoints waypoints{
	    {Ai::Sphere{registry.get<Sim::Transform>(berth.platform).position, radius}}, clearance};
	const glm::dquat inverse = glm::conjugate(berthAt.orientation);
	if (const std::optional<Ai::Course> course = waypoints.PlotCourse(
	        registry.get<Sim::Transform>(vehicle).position,
	        berthAt.position + berthAt.orientation * mouth))
		for (std::size_t point = 1; point + 1 < course->points.size(); ++point)
			destination.waypoints.push_back(inverse * (course->points[point] - berthAt.position));
	destination.waypoints.push_back(mouth);
	(void)time;
	registry.emplace_or_replace<Ai::Destination>(vehicle, std::move(destination));
}


void Docks::FlyDown(Sim::SimWorld &world, entt::entity vehicle)
{
	entt::registry &registry = world.Registry();
	const Docking &docking = registry.get<Docking>(vehicle);
	Steer(registry, vehicle);
	Ai::Destination destination;
	destination.entity = docking.berth;
	destination.offset = {0.0, RestingHeight(*VehicleOf(registry, vehicle)) + kHover, 0.0};
	destination.byShape = true;
	destination.face = true;
	destination.speed = kDescentSpeed;
	destination.arrivalRadius = kLandingRadius;
	destination.arrivalSpeed = kLandingSpeed;
	registry.emplace_or_replace<Ai::Destination>(vehicle, std::move(destination));
}


void Docks::FlyUp(Sim::SimWorld &world, entt::entity vehicle)
{
	entt::registry &registry = world.Registry();
	const Docking &docking = registry.get<Docking>(vehicle);
	Steer(registry, vehicle);

	// From under a cover, up to the point over the berth and out by the shortest way out from under
	// it, beyond the platform's sphere, where it gives the berth up.
	const Berth &berth = registry.get<Berth>(docking.berth);
	if (const std::optional<double> covered = CoveredHeight(registry, vehicle, berth))
	{
		const Sim::Transform &berthAt = registry.get<Sim::Transform>(docking.berth);
		const glm::dvec3 over = berthAt.position + berthAt.orientation * glm::dvec3{0.0, *covered, 0.0};
		std::optional<Ai::Course> shortest;
		for (const glm::dvec3 &out : WaysOut(registry, vehicle, berth))
			if (std::optional<Ai::Course> course = CourseAmong(registry, vehicle, berth, over, out);
			    course && (!shortest || course->length < shortest->length))
				shortest = std::move(course);
		Ai::Destination destination;
		destination.entity = docking.berth;
		destination.waypointsOnEntity = true;
		destination.byShape = true;
		destination.speed = kCoveredSpeed;
		destination.passRadius = kCoveredPass;
		destination.arrivalRadius = kAboveRadius;
		destination.arrivalSpeed = kAboveSpeed;
		const glm::dquat inverse = glm::conjugate(berthAt.orientation);
		destination.waypoints.emplace_back(0.0, *covered, 0.0);
		if (shortest)
		{
			for (std::size_t point = 1; point + 1 < shortest->points.size(); ++point)
				destination.waypoints.push_back(inverse * (shortest->points[point] - berthAt.position));
			destination.offset = inverse * (shortest->points.back() - berthAt.position);
		}
		else
		{
			Core::Log(
			    Core::Severity::Warning, kDock,
			    "No way out from among the decks and piers from berth {} was found; it is flown up",
			    berth.number);
			destination.offset = {0.0, AboveHeight(registry, vehicle, berth), 0.0};
		}
		registry.emplace_or_replace<Ai::Destination>(vehicle, std::move(destination));
		return;
	}

	Ai::Destination destination;
	destination.entity = docking.berth;
	destination.offset = {0.0, AboveHeight(registry, vehicle, registry.get<Berth>(docking.berth)), 0.0};
	destination.byShape = true;
	destination.face = true;
	destination.speed = kLiftSpeed;
	destination.arrivalRadius = kAboveRadius;
	destination.arrivalSpeed = kAboveSpeed;
	registry.emplace_or_replace<Ai::Destination>(vehicle, std::move(destination));
}


bool Docks::InClampReach(const entt::registry &registry, entt::entity vehicle) const
{
	const Docking &docking = registry.get<Docking>(vehicle);
	const Sim::Transform &berthAt = registry.get<Sim::Transform>(docking.berth);
	const Sim::Transform &vehicleAt = registry.get<Sim::Transform>(vehicle);
	const glm::dquat inverse = glm::conjugate(berthAt.orientation);
	const glm::dvec3 offset = inverse * (vehicleAt.position - berthAt.position) -
	                          glm::dvec3{0.0, RestingHeight(*VehicleOf(registry, vehicle)), 0.0};
	if (std::hypot(offset.x, offset.z) > kClampAcross || std::abs(offset.y) > kClampAbove)
		return false;
	const glm::dquat turned = inverse * vehicleAt.orientation;
	if (glm::dot(turned * glm::dvec3{0.0, 1.0, 0.0}, glm::dvec3{0.0, 1.0, 0.0}) < kClampUp ||
	    glm::dot(turned * glm::dvec3{0.0, 0.0, -1.0}, glm::dvec3{0.0, 0.0, -1.0}) < kClampNose)
		return false;
	const auto *velocity = registry.try_get<Physics::Velocity>(vehicle);
	return !velocity || glm::length(glm::dvec3{velocity->linear}) <= kClampSpeed;
}


void Docks::Clamp(Sim::SimWorld &world, entt::entity vehicle, double time)
{
	entt::registry &registry = world.Registry();
	Docking &docking = registry.get<Docking>(vehicle);
	Berth &berth = registry.get<Berth>(docking.berth);
	const BerthLayout &place = PlaceOf(registry, berth);
	const Physics::OnRails &platform = registry.get<Physics::OnRails>(berth.platform);
	const glm::dquat turn = BerthOrientation(place);
	const glm::dvec3 rest =
	    place.position + turn * glm::dvec3{0.0, RestingHeight(*VehicleOf(registry, vehicle)), 0.0};
	const Physics::OnRails rails{
	    platform.body, platform.offset + platform.turn * rest, platform.turn * turn};
	registry.emplace_or_replace<Physics::OnRails>(vehicle, rails);
	registry.replace<Sim::Transform>(
	    vehicle, m_physics.RailsPlace(FrameOf(registry, vehicle), rails, time));
	Physics::Body body = registry.get<Physics::Body>(vehicle);
	body.kind = Physics::BodyKind::Kinematic;
	registry.replace<Physics::Body>(vehicle, body);
	registry.emplace_or_replace<Physics::Velocity>(vehicle);
	Hold(registry, vehicle);
	docking.stage = Stage::Docked;
	berth.state = BerthState::Holding;
	berth.vehicle = vehicle;
}


void Docks::Unclamp(Sim::SimWorld &world, entt::entity vehicle)
{
	entt::registry &registry = world.Registry();
	Docking &docking = registry.get<Docking>(vehicle);
	Physics::Body body = registry.get<Physics::Body>(vehicle);
	body.kind = Physics::BodyKind::Dynamic;
	registry.replace<Physics::Body>(vehicle, body);
	registry.remove<Physics::OnRails>(vehicle);
	registry.emplace_or_replace<Physics::Velocity>(vehicle);
	docking.stage = Stage::Leaving;
	world.Events().Emit(
	    Left{vehicle, docking.platform, docking.berth, registry.get<Berth>(docking.berth).number});
	if (docking.fly)
		FlyUp(world, vehicle);
	else
		Lift(registry, vehicle);
}


void Docks::Lift(entt::registry &registry, entt::entity vehicle) const
{
	const Docking &docking = registry.get<Docking>(vehicle);
	const Physics::Handling &handling = registry.get<Physics::Handling>(vehicle);
	registry.remove<Ai::Destination>(vehicle);

	// Under a cover, slowing to stop at the point over the berth, braking as steering brakes, so it
	// does not climb on into the roof once it is let go there.
	double speed = kLiftSpeed;
	if (CoveredHeight(registry, vehicle, registry.get<Berth>(docking.berth)))
		speed = std::min(
		    speed, std::sqrt(
		               2.0 * Ai::BrakingAcceleration(handling) *
		               std::max(0.0, LiftHeight(registry, vehicle) - HeightOver(registry, vehicle))));

	// Clamped, the vehicle's up is its berth's; with assist, a demand along it is that share of
	// maxSpeed.
	registry.replace<Physics::FlightControls>(
	    vehicle, Physics::FlightControls{
	                 {0.0f, std::min(static_cast<float>(speed) / handling.maxSpeed, 1.0f), 0.0f},
	                 {},
	                 true,
	                 docking.platform});
}


double Docks::HeightOver(const entt::registry &registry, entt::entity vehicle) const
{
	const Sim::Transform &berthAt = registry.get<Sim::Transform>(registry.get<Docking>(vehicle).berth);
	const Sim::Transform &vehicleAt = registry.get<Sim::Transform>(vehicle);
	return (glm::conjugate(berthAt.orientation) * (vehicleAt.position - berthAt.position)).y;
}


double Docks::LiftHeight(const entt::registry &registry, entt::entity vehicle) const
{
	const double rest = RestingHeight(*VehicleOf(registry, vehicle));

	// Under a cover, as high as the point over the berth, which is as far as straight up goes.
	if (const std::optional<double> covered =
	        CoveredHeight(registry, vehicle, registry.get<Berth>(registry.get<Docking>(vehicle).berth)))
		return std::min(*covered, rest + kLiftHeight);
	return rest + kLiftHeight;
}


bool Docks::Lifted(const entt::registry &registry, entt::entity vehicle) const
{
	// Under a cover it slows to the point over the berth, so it is there once within a hover of it.
	const bool covered =
	    CoveredHeight(registry, vehicle, registry.get<Berth>(registry.get<Docking>(vehicle).berth))
	        .has_value();
	return HeightOver(registry, vehicle) >= LiftHeight(registry, vehicle) - (covered ? kHover : 0.0);
}


std::optional<double> Docks::CoveredHeight(
    const entt::registry &registry, entt::entity vehicle, const Berth &berth) const
{
	const BerthLayout &place = PlaceOf(registry, berth);
	const double headroom = Headroom(
	    m_layouts.at(registry.get<Platform>(berth.platform).layout), place,
	    m_berthClasses.at(place.berthClass).berthSize);
	if (!std::isfinite(headroom))
		return std::nullopt;
	return std::max(RestingHeight(*VehicleOf(registry, vehicle)) + kHover, headroom * kCoveredShare);
}


std::vector<glm::dvec3> Docks::WaysOut(
    const entt::registry &registry, entt::entity vehicle, const Berth &berth) const
{
	const Sim::Transform &berthAt = registry.get<Sim::Transform>(registry.get<Docking>(vehicle).berth);
	const double reach =
	    2.0 * Radius(m_layouts.at(registry.get<Platform>(berth.platform).layout)) * kWayOutReach;
	const glm::dvec3 over =
	    berthAt.position +
	    berthAt.orientation *
	        glm::dvec3{0.0, CoveredHeight(registry, vehicle, berth).value_or(0.0), 0.0};
	std::vector<glm::dvec3> ways;
	for (const glm::dvec3 &side :
	     {glm::dvec3{1.0, 0.0, 0.0}, glm::dvec3{-1.0, 0.0, 0.0}, glm::dvec3{0.0, 0.0, 1.0},
	      glm::dvec3{0.0, 0.0, -1.0}})
		ways.push_back(over + berthAt.orientation * (side * reach));
	return ways;
}


std::optional<Ai::Course> Docks::CourseAmong(
    const entt::registry &registry, entt::entity vehicle, const Berth &berth, const glm::dvec3 &from,
    const glm::dvec3 &to) const
{
	const Sim::Transform &platformAt = registry.get<Sim::Transform>(berth.platform);
	std::vector<Ai::Box> boxes;
	for (const Box &box : Structure(m_layouts.at(registry.get<Platform>(berth.platform).layout)))
		boxes.push_back(
		    {platformAt.position + platformAt.orientation * box.centre, box.size * 0.5,
		     platformAt.orientation});
	const auto *steered = registry.try_get<Ai::Steered>(vehicle);
	const double clearance = m_physics.ShapeRadius(registry.get<Physics::Body>(vehicle).shape) +
	                         double{steered ? steered->clearance : Ai::Steered{}.clearance};

	// Resting on a deck within half its hover of its resting height: nearer than any point over a
	// berth is, which is a hover above it or more, so a course from there leaves it level. Never more
	// than the clearance, which a small vehicle steered close may have less of.
	const double resting =
	    std::min(clearance, RestingHeight(*VehicleOf(registry, vehicle)) + kHover * kRestingShare);
	const Ai::Waypoints waypoints{
	    {}, clearance, WaysOut(registry, vehicle, berth), std::move(boxes), resting};
	return waypoints.PlotCourse(from, to);
}


const BerthLayout &Docks::PlaceOf(const entt::registry &registry, const Berth &berth) const
{
	return m_layouts.at(registry.get<Platform>(berth.platform).layout).berths.at(berth.number - 1);
}


const VehicleDefinition *Docks::VehicleOf(const entt::registry &registry, entt::entity vehicle) const
{
	const auto *named = registry.try_get<Vehicle>(vehicle);
	return named ? FindVehicle(named->definition) : nullptr;
}


double Docks::AboveHeight(
    const entt::registry &registry, entt::entity vehicle, const Berth &berth) const
{
	const BerthLayout &place = PlaceOf(registry, berth);
	const auto *steered = registry.try_get<Ai::Steered>(vehicle);
	const double reach = Radius(m_layouts.at(registry.get<Platform>(berth.platform).layout)) +
	                     m_physics.ShapeRadius(registry.get<Physics::Body>(vehicle).shape) +
	                     double{steered ? steered->clearance : Ai::Steered{}.clearance} + kAboveMargin;

	// Where the line out along the normal from the berth crosses that sphere: |p + n h| = reach.
	const double along = glm::dot(place.position, place.normal);
	return -along + std::sqrt(along * along - glm::dot(place.position, place.position) + reach * reach);
}


std::uint64_t Docks::ReservationSteps(const Sim::SimWorld &world, const PlatformLayout &layout)
{
	return static_cast<std::uint64_t>(std::ceil(double{layout.reservationSeconds} / world.FixedStep()));
}


double Docks::RestingHeight(const VehicleDefinition &vehicle)
{
	return double{vehicle.height} * 0.5;
}


ClampGrips Docks::GripsOf(std::string_view name, const VehicleDefinition &vehicle) const
{
	// From the berth's axes to the vehicle's, which rests its centre over the berth.
	const glm::dvec3 rest{0.0, RestingHeight(vehicle), 0.0};
	const std::string shape = VehicleShape(name);
	ClampGrips grips;
	for (std::size_t index = 0; index < kClamps; ++index)
	{
		const double side = index < kClamps / 2 ? 1.0 : -1.0;
		const double along = (index % 2 == 0 ? -kClampAlong : kClampAlong) * double{vehicle.length};
		const glm::dvec3 onBox{
		    side * 0.5 * double{vehicle.beam}, kClampGripHeight * double{vehicle.height}, along};
		if (vehicle.collision.empty())
		{
			grips.at(index) = onBox;
			continue;
		}
		if (const std::optional<Physics::CastHit> hit = m_physics.CastRayOnShape(
		        shape, onBox - rest, {-side, 0.0, 0.0}, kClampTravel * double{vehicle.beam}))
			grips.at(index) = hit->point + rest;
	}
	return grips;
}
} // namespace Jettison::Dock
