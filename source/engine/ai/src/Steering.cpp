#include <jettison/ai/Steering.h>

#include <jettison/ai/Types.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/profile/Profile.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/FrameIndices.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace Jettison::Ai
{
namespace
{
// Within the last metres of an arrival, or of an obstacle's clearance, the speed is no more than
// the distance over this: an approach that slows as it nears, where √(2ad) alone, its slope
// unbounded at the end, would be overshot a step at a time.
constexpr double kSettleSeconds = 1.0;

// The soonest an avoidance plans to have made up its shortfall by, so a threat already at hand is
// turned from fast but not at an unbounded speed.
constexpr double kSoonestSeconds = 0.5;

// The share of a vehicle's weakest thruster it brakes with, and of its angular acceleration it stops
// a turn with: the rest is for turning and avoiding while it does.
constexpr double kBrakingShare = 0.5;
constexpr double kTurnShare = 0.5;

// Below this speed a vehicle holds its heading rather than turn to a velocity that is mostly the
// last few centimetres of an arrival.
constexpr double kFacingSpeed = 1.0;

// How much further than its target a vehicle kept clear by its shape may meet something, in metres, and
// still be going to its target rather than into what it meets: a landing's target hovers a little
// above the deck.
constexpr double kShapeSlack = 0.5;


glm::dvec3 HeldTo(const glm::dvec3 &velocity, double maxSpeed)
{
	const double speed = glm::length(velocity);
	return speed > maxSpeed && speed > 0.0 ? velocity * (maxSpeed / speed) : velocity;
}


// A direction square to the vector, chosen by the vector alone: crossed with the axis least along
// it, so the vector reversed gives the direction reversed.
glm::dvec3 Square(const glm::dvec3 &vector)
{
	const glm::dvec3 size = glm::abs(vector);
	glm::dvec3 axis{0.0};
	if (size.x <= size.y && size.x <= size.z)
		axis.x = 1.0;
	else if (size.y <= size.z)
		axis.y = 1.0;
	else
		axis.z = 1.0;
	return glm::normalize(glm::cross(vector, axis));
}


// The speed to close with what is the gap away at, stopping as it closes: √(2ad), and within the
// last metres the gap over kSettleSeconds. A gap that is negative, within, is backed out of at
// that rate.
double ClosingSpeed(double gap, double acceleration)
{
	if (gap <= 0.0)
		return gap / kSettleSeconds;
	return std::min(std::sqrt(2.0 * acceleration * gap), gap / kSettleSeconds);
}


// A capability, none if it is not a positive number, as the flight model reads one.
double Capability(float value)
{
	return value > 0.0f && std::isfinite(value) ? double{value} : 0.0;
}


// A turn of the angle about the axis, in the vehicle's axes, as rates a share of each axis's most: at
// the rate the turn can still be stopped from at a share of the angular acceleration, about the
// axes it is given (pitch and yaw, or all three).
glm::vec3 Turn(
    const Physics::Handling &handling, const glm::dvec3 &axis, double angle, glm::length_t axes)
{
	double maxRate = std::numeric_limits<double>::infinity();
	double acceleration = std::numeric_limits<double>::infinity();
	for (glm::length_t index = 0; index < axes; ++index)
	{
		maxRate = std::min(maxRate, Capability(handling.maxAngularRate[index]));
		acceleration = std::min(acceleration, Capability(handling.angularAcceleration[index]));
	}
	const glm::dvec3 turn =
	    axis * std::min(maxRate, std::sqrt(2.0 * kTurnShare * acceleration * angle));
	glm::vec3 demand{0.0f};
	for (glm::length_t index = 0; index < axes; ++index)
	{
		const double rate = Capability(handling.maxAngularRate[index]);
		demand[index] =
		    rate > 0.0 ? static_cast<float>(std::clamp(turn[index] / rate, -1.0, 1.0)) : 0.0f;
	}
	return demand;
}


// The controls that fly the velocity: with assist, a share of maxSpeed along each of the vehicle's
// axes, and a turn of the nose (-Z) towards it about the axis square to both, at the rate the turn
// can still be stopped from; or, given a facing, a turn to that orientation, rolled too.
Physics::FlightControls Controls(
    const glm::dquat &orientation, const Physics::Handling &handling, const glm::dvec3 &velocity,
    const glm::dquat *facing)
{
	Physics::FlightControls controls;
	controls.assist = true;
	const glm::dquat inverse = glm::conjugate(glm::normalize(orientation));
	if (const double maxSpeed = Capability(handling.maxSpeed); maxSpeed > 0.0)
		controls.linear = glm::vec3{glm::clamp(inverse * velocity / maxSpeed, -1.0, 1.0)};

	if (facing)
	{
		// The rest of the turn, in the vehicle's axes, the short way round.
		glm::dquat rest = inverse * glm::normalize(*facing);
		if (rest.w < 0.0)
			rest = -rest;
		const glm::dvec3 vector{rest.x, rest.y, rest.z};
		const double sine = glm::length(vector);
		if (sine > 1.0e-9)
			controls.angular =
			    Turn(handling, vector / sine, 2.0 * std::atan2(sine, rest.w), glm::length_t{3});
		return controls;
	}

	const double speed = glm::length(velocity);
	if (speed <= kFacingSpeed)
		return controls;
	const glm::dvec3 heading = inverse * (velocity / speed);
	const glm::dvec3 nose{0.0, 0.0, -1.0};
	glm::dvec3 axis = glm::cross(nose, heading);
	const double sine = glm::length(axis);
	const double angle = std::atan2(sine, glm::dot(nose, heading));
	if (sine > 1.0e-9)
		axis /= sine;
	else if (angle > 0.0)
		axis = {0.0, 1.0, 0.0};
	else
		return controls;

	controls.angular = Turn(handling, axis, angle, glm::length_t{2});
	return controls;
}


std::string_view LocalFrameOf(const entt::registry &registry, entt::entity entity)
{
	const auto *frame = registry.try_get<Physics::LocalFrame>(entity);
	return frame ? std::string_view{frame->body} : std::string_view{};
}


void RefuseDestination(Sim::SimWorld &world, const Sim::Controller &sender, entt::entity vehicle)
{
	world.Events().Emit(DestinationRefused{sender, vehicle, Sim::MakeReason(Reason::NotController)});
}
} // namespace


glm::dvec3 Seek(const Agent &agent, const glm::dvec3 &target)
{
	const glm::dvec3 way = target - agent.position;
	const double distance = glm::length(way);
	return distance > 0.0 ? way * (agent.maxSpeed / distance) : glm::dvec3{0.0};
}


glm::dvec3 Arrive(const Agent &agent, const glm::dvec3 &target, const glm::dvec3 &targetVelocity)
{
	const glm::dvec3 way = target - agent.position;
	const double distance = glm::length(way);
	if (distance <= 0.0)
		return targetVelocity;
	const double speed = std::min(agent.maxSpeed, ClosingSpeed(distance, agent.maxAcceleration));
	return targetVelocity + way * (speed / distance);
}


namespace
{
// How much sooner than it reaches the end of a corridor a vehicle is back on its line (ArriveAlong).
constexpr double kLineSpare = 2.0;
} // namespace


glm::dvec3 ArriveAlong(
    const Agent &agent, const glm::dvec3 &start, const glm::dvec3 &target,
    const glm::dvec3 &targetVelocity)
{
	const glm::dvec3 line = target - start;
	const double length = glm::length(line);
	if (length <= 0.0)
		return Arrive(agent, target, targetVelocity);
	const glm::dvec3 along = line / length;

	// How far it has still to go along the line, and how far off it it is.
	const glm::dvec3 from = agent.position - target;
	const double left = -glm::dot(from, along);
	const glm::dvec3 off = from + along * left;
	const double offset = glm::length(off);
	double closing = std::min(agent.maxSpeed, ClosingSpeed(std::abs(left), agent.maxAcceleration));
	const double back = std::min(agent.maxSpeed, ClosingSpeed(offset, agent.maxAcceleration));

	// No faster down the line than it comes back onto it, with time to spare, so it is on the line
	// before the end of it: the way narrows to the target as a cone does.
	if (offset > 0.0 && back > 0.0)
		closing = std::min(closing, back * std::abs(left) / (offset * kLineSpare));
	glm::dvec3 velocity = along * (left >= 0.0 ? closing : -closing);
	if (offset > 0.0)
		velocity -= off * (back / offset);
	const double speed = glm::length(velocity);
	if (speed > agent.maxSpeed)
		velocity *= agent.maxSpeed / speed;
	return targetVelocity + velocity;
}


double BrakingAcceleration(const Physics::Handling &handling)
{
	return kBrakingShare * std::min(
	                           {Capability(handling.reverse), Capability(handling.lateral),
	                            Capability(handling.vertical)});
}


glm::dvec3 Avoid(
    const Agent &agent, const glm::dvec3 &desired, std::span<const Obstacle> obstacles,
    double lookahead, double clearance)
{
	// The first the desired velocity would bring the agent within the clearance of: when, and how
	// near it would pass.
	double soonest = std::numeric_limits<double>::infinity();
	glm::dvec3 miss{0.0};
	glm::dvec3 motion{0.0};
	double shortfall = 0.0;
	for (const Obstacle &obstacle : obstacles)
	{
		const glm::dvec3 offset = obstacle.position - agent.position;
		const double reach = agent.radius + obstacle.radius + clearance;
		const glm::dvec3 relative = desired - obstacle.velocity;
		double time = 0.0;
		glm::dvec3 closest = offset;
		if (glm::dot(offset, offset) >= reach * reach)
		{
			const double closing = glm::dot(relative, relative);
			if (closing <= 0.0)
				continue;
			time = glm::dot(offset, relative) / closing;
			if (time <= 0.0 || time > lookahead)
				continue;
			closest = offset - relative * time;
			if (glm::dot(closest, closest) >= reach * reach)
				continue;
		}
		if (time < soonest)
		{
			soonest = time;
			miss = closest;
			motion = relative;
			shortfall = reach - glm::length(closest);
		}
	}

	glm::dvec3 velocity = desired;
	if (std::isfinite(soonest))
	{
		const double missed = glm::length(miss);
		glm::dvec3 away{0.0};
		if (missed > 1.0e-6)
			away = -miss / missed;
		else if (glm::dot(motion, motion) > 0.0)
			away = Square(motion);
		velocity += away * (shortfall / std::max(soonest, kSoonestSeconds));
	}

	for (const Obstacle &obstacle : obstacles)
	{
		const glm::dvec3 offset = obstacle.position - agent.position;
		const double distance = glm::length(offset);
		if (distance <= 0.0)
			continue;
		const glm::dvec3 towards = offset / distance;
		const double gap = distance - (agent.radius + obstacle.radius + clearance);
		const double allowed = ClosingSpeed(gap, agent.maxAcceleration);
		const double closing = glm::dot(velocity - obstacle.velocity, towards);
		if (closing > allowed)
			velocity -= towards * (closing - allowed);
	}
	return HeldTo(velocity, agent.maxSpeed);
}


glm::dvec3 Separate(const Agent &agent, std::span<const Obstacle> neighbours, double separation)
{
	glm::dvec3 push{0.0};
	for (const Obstacle &neighbour : neighbours)
	{
		const glm::dvec3 offset = agent.position - neighbour.position;
		const double distance = glm::length(offset);
		const double reach = agent.radius + neighbour.radius + separation;
		if (distance <= 0.0 || distance >= reach)
			continue;
		push += offset * ((reach - distance) / (distance * kSettleSeconds));
	}
	return HeldTo(push, agent.maxSpeed);
}


Steering &Steering::Attach(
    Sim::SimWorld &world, const Physics::PhysicsWorld &physics, const Spatial::FrameIndices &indices)
{
	RegisterTypes();

	// Shared by the commands and the system, which the world keeps for as long as it lives.
	std::shared_ptr<Steering> steering{new Steering{physics, indices}};
	world.RegisterComponent<Steered>();
	world.RegisterComponent<Destination>();
	world.RegisterCommand<SetDestination>(
	    [](Sim::SimWorld &target, const SetDestination &command)
	    {
		    entt::registry &registry = target.Registry();
		    if (!registry.valid(command.vehicle))
			    return;
		    if (!Sim::MayCommand(registry, command.vehicle, command.sender))
			    return RefuseDestination(target, command.sender, command.vehicle);
		    Destination destination = command.destination;
		    destination.next = 0;
		    destination.arrived = false;
		    registry.emplace_or_replace<Destination>(command.vehicle, std::move(destination));
	    });
	world.RegisterCommand<ClearDestination>(
	    [](Sim::SimWorld &target, const ClearDestination &command)
	    {
		    entt::registry &registry = target.Registry();
		    if (!registry.valid(command.vehicle))
			    return;
		    if (!Sim::MayCommand(registry, command.vehicle, command.sender))
			    return RefuseDestination(target, command.sender, command.vehicle);
		    registry.remove<Destination>(command.vehicle);
		    if (auto *controls = registry.try_get<Physics::FlightControls>(command.vehicle))
			    *controls = Physics::FlightControls{};
	    });
	Ecs::SystemDescription system{
	    std::string{kSystemName},
	    Ecs::TickPhase::Simulation,
	    Ecs::Access::Of<
	        Steered, Physics::Handling, Physics::Velocity, Physics::Body, Physics::LocalFrame,
	        Sim::Transform, Sim::InFrame>()
	        .Writing<Destination, Physics::FlightControls>(),
	    {std::string{Spatial::FrameIndices::kSystemName}},
	    [steering, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	    { steering->Step(world); }};
	system.before = {std::string{Physics::kFlightSystemName}};

	// A vehicle with somewhere to go in a frame is flown there every step.
	world.RegisterSystem(
	    std::move(system),
	    {.next = [](const Sim::SimWorld &target)
	     {
		     const entt::registry &registry = target.Registry();
		     for (const entt::entity vehicle :
		          registry.view<
		              const Steered, const Destination, const Physics::Handling,
		              const Physics::FlightControls, const Physics::Velocity, const Physics::Body,
		              const Sim::Transform, const Sim::InFrame>())
			     if (registry.get<const Sim::InFrame>(vehicle).frame != Sim::kNoFrame)
				     return target.Clock().Step();
		     return std::numeric_limits<std::uint64_t>::max();
	     }});
	return *steering;
}


glm::dvec3 Steering::ShapeLimited(
    std::uint64_t frame, entt::entity vehicle, const Physics::Body &body, const Sim::Transform &at,
    const Agent &agent, const glm::dvec3 &velocity, const glm::dvec3 &target, double clearance) const
{
	const double speed = glm::length(velocity);
	if (speed <= 1.0e-6)
		return velocity;
	const glm::dvec3 way = velocity / speed;

	// As far as it could need to stop from this speed, and the clearance past that.
	const double reach = speed * speed / (2.0 * std::max(agent.maxAcceleration, 1.0e-6)) +
	                     speed * kSettleSeconds + clearance + kShapeSlack;
	const std::optional<Physics::CastHit> hit = m_physics.CastShape(
	    frame, body.shape, at, way, reach, {vehicle, false, std::nullopt, body.layer});
	if (!hit)
		return velocity;

	// What it meets beyond where it is going, the deck under a berth it lands on, is not in its way.
	if (glm::distance(at.position, target) <= hit->distance + kShapeSlack)
		return velocity;
	const double allowed = ClosingSpeed(hit->distance - clearance, agent.maxAcceleration);

	// Within the clearance it backs out, unless that way is as near something else, as a vehicle
	// between a deck and the roof over it is: it holds there, turning as it was going to.
	if (allowed < 0.0)
	{
		const std::optional<Physics::CastHit> behind = m_physics.CastShape(
		    frame, body.shape, at, -way, clearance + kShapeSlack,
		    {vehicle, false, std::nullopt, body.layer});
		if (behind && behind->distance < clearance)
			return glm::dvec3{0.0};
	}
	return speed > allowed ? velocity * (allowed / speed) : velocity;
}


double Steering::Radius(std::string_view shape)
{
	const auto found = m_radii.find(shape);
	if (found != m_radii.end())
		return found->second;
	return m_radii.emplace(std::string{shape}, m_physics.ShapeRadius(shape)).first->second;
}


void Steering::Step(Sim::SimWorld &world)
{
	JETTISON_PROFILE_ZONE("Ai::Steering::Step");
	entt::registry &registry = world.Registry();
	const double now = world.Clock().Now();
	std::vector<entt::entity> vehicles;
	for (const entt::entity vehicle :
	     registry.view<
	         const Steered, Destination, const Physics::Handling, const Physics::FlightControls,
	         const Physics::Velocity, const Physics::Body, const Sim::Transform, const Sim::InFrame>())
		vehicles.push_back(vehicle);
	std::ranges::sort(vehicles, {}, [](entt::entity vehicle) { return entt::to_integral(vehicle); });

	// Each frame's origin as the step begins, as it is asked for.
	std::map<std::pair<std::uint64_t, std::string>, glm::dvec3> origins;
	const auto originOf = [&](std::uint64_t frame, std::string_view localFrame) -> const glm::dvec3 &
	{
		std::pair<std::uint64_t, std::string> key{frame, std::string{localFrame}};
		auto found = origins.find(key);
		if (found == origins.end())
			found =
			    origins.emplace(std::move(key), m_physics.FrameOrigin(frame, localFrame, now)).first;
		return found->second;
	};

	const double largest = m_physics.LargestShapeRadius();
	const Physics::LayerDefinitions &layers = m_physics.Layers();
	std::vector<entt::entity> near;
	std::vector<Obstacle> obstacles;
	std::vector<Obstacle> neighbours;
	for (const entt::entity vehicle : vehicles)
	{
		const std::uint64_t frame = registry.get<const Sim::InFrame>(vehicle).frame;
		if (frame == Sim::kNoFrame)
			continue;
		const Steered &steered = registry.get<const Steered>(vehicle);
		Destination &destination = registry.get<Destination>(vehicle);
		const Physics::Handling &handling = registry.get<const Physics::Handling>(vehicle);
		const Sim::Transform &transform = registry.get<const Sim::Transform>(vehicle);
		const Physics::Body &own = registry.get<const Physics::Body>(vehicle);
		const std::string_view localFrame = LocalFrameOf(registry, vehicle);
		const glm::dvec3 &origin = originOf(frame, localFrame);

		// Where the destination is, how it moves, and which way to face there.
		glm::dvec3 target = origin + destination.offset;
		glm::dvec3 targetVelocity{0.0};
		glm::dquat facing = destination.facing;

		// The rails of what is kept clear of by its shape, none if nothing is; and where the
		// waypoints are reckoned from, and how they are turned.
		std::string_view shapeRails;
		glm::dvec3 course = origin;
		glm::dquat courseTurn{1.0, 0.0, 0.0, 0.0};
		if (destination.entity != entt::null)
		{
			const entt::entity entity = destination.entity;
			std::optional<Reason> lost;
			if (!registry.valid(entity) || !registry.all_of<Sim::Transform, Sim::InFrame>(entity))
				lost = Reason::DestinationGone;
			else if (registry.get<const Sim::InFrame>(entity).frame != frame)
				lost = Reason::DestinationOtherFrame;
			else if (LocalFrameOf(registry, entity) != localFrame)
				lost = Reason::DestinationOtherLocalFrame;
			if (lost)
			{
				registry.remove<Destination>(vehicle);
				registry.replace<Physics::FlightControls>(vehicle);
				world.Events().Emit(DestinationLost{vehicle, Sim::MakeReason(*lost)});
				continue;
			}
			const Sim::Transform &at = registry.get<const Sim::Transform>(entity);
			if (destination.waypointsOnEntity)
			{
				course = at.position;
				courseTurn = at.orientation;
			}
			const glm::dvec3 offset = at.orientation * destination.offset;
			target = at.position + offset;
			facing = at.orientation * destination.facing;
			if (const auto *rails = registry.try_get<Physics::OnRails>(entity);
			    rails && destination.byShape)
				shapeRails = rails->body;
			if (const auto *moving = registry.try_get<Physics::Velocity>(entity))
				targetVelocity =
				    glm::dvec3{moving->linear} + glm::cross(glm::dvec3{moving->angular}, offset);
		}
		const auto waypoint = [&](std::uint32_t index)
		{ return course + courseTurn * destination.waypoints[index]; };
		while (destination.next < destination.waypoints.size() &&
		       glm::distance(transform.position, waypoint(destination.next)) <=
		           double{destination.passRadius})
			++destination.next;

		const double maxSpeed = destination.speed > 0.0f
		                            ? std::min(double{destination.speed}, Capability(handling.maxSpeed))
		                            : Capability(handling.maxSpeed);
		const Agent agent{
		    transform.position, glm::dvec3{registry.get<const Physics::Velocity>(vehicle).linear},
		    maxSpeed, BrakingAcceleration(handling), Radius(own.shape)};

		// What is near, in its frame.
		obstacles.clear();
		neighbours.clear();
		const double lookahead = std::max(0.0, double{steered.lookahead});
		const double clearance = std::max(0.0, double{steered.clearance});
		const double separation = std::max(0.0, double{steered.separation});
		if (const Spatial::SpatialIndex *index = m_indices.Find(frame))
		{
			const double reach =
			    lookahead * 2.0 * maxSpeed + agent.radius + largest + std::max(clearance, separation);
			index->QuerySphere(agent.position, reach, near);
			for (const entt::entity entity : near)
			{
				const auto *body = registry.try_get<Physics::Body>(entity);
				if (entity == vehicle || !body || body->sensor ||
				    LocalFrameOf(registry, entity) != localFrame)
					continue;

				// What it would pass through is not in its way.
				if (!Physics::LayersMeet(layers, own.layer, body->layer) ||
				    (own.group && body->group == own.group))
					continue;
				if (destination.byShape)
				{
					const auto *rails = registry.try_get<Physics::OnRails>(entity);
					if (entity == destination.entity || (rails && rails->body == shapeRails))
						continue;
				}
				const auto *moving = registry.try_get<Physics::Velocity>(entity);
				const Obstacle obstacle{
				    registry.get<const Sim::Transform>(entity).position,
				    moving ? glm::dvec3{moving->linear} : glm::dvec3{0.0}, Radius(body->shape)};
				obstacles.push_back(obstacle);
				if (body->kind == Physics::BodyKind::Dynamic)
					neighbours.push_back(obstacle);
			}
		}

		const bool onCourse = destination.next < destination.waypoints.size();
		glm::dvec3 desired{0.0};
		if (onCourse)
		{
			desired = Seek(agent, waypoint(destination.next));

			// In a corridor, or among what it is kept clear of by its shape, no faster than it can
			// still stop in the whole way left, and come to the waypoint no faster than its turn
			// there allows: as fast as the way after it lets it go straight on, slowing as the turn
			// sharpens, to a stop for one that turns back. A course among a structure's boxes turns
			// close by them, where carrying on past the waypoint would take it into one.
			if (destination.corridor || destination.byShape)
			{
				const auto last = static_cast<std::uint32_t>(destination.waypoints.size() - 1);
				const glm::dvec3 corner = waypoint(destination.next);
				const glm::dvec3 after =
				    destination.next < last ? waypoint(destination.next + 1) : target;
				double beyond = glm::distance(corner, after);
				for (std::uint32_t index = destination.next + 1; index < last; ++index)
					beyond += glm::distance(waypoint(index), waypoint(index + 1));
				if (destination.next < last)
					beyond += glm::distance(waypoint(last), target);
				const glm::dvec3 in = corner - agent.position;
				const glm::dvec3 out = after - corner;
				const double turn =
				    glm::length(in) > 0.0 && glm::length(out) > 0.0
				        ? (1.0 + glm::dot(glm::normalize(in), glm::normalize(out))) * 0.5
				        : 1.0;
				const double atCorner =
				    std::min(agent.maxSpeed, ClosingSpeed(beyond, agent.maxAcceleration)) * turn;
				const double toCorner = glm::length(in);
				desired = HeldTo(
				    desired, std::sqrt(atCorner * atCorner + 2.0 * agent.maxAcceleration * toCorner));
			}
		}
		else if (destination.corridor && !destination.waypoints.empty())
			desired = ArriveAlong(
			    agent, waypoint(static_cast<std::uint32_t>(destination.waypoints.size() - 1)), target,
			    targetVelocity);
		else
			desired = Arrive(agent, target, targetVelocity);
		glm::dvec3 velocity = HeldTo(
		    Avoid(agent, desired, obstacles, lookahead, clearance) +
		        Separate(agent, neighbours, separation),
		    maxSpeed);
		if (destination.byShape)
			velocity = ShapeLimited(
			    frame, vehicle, own, transform, agent, velocity,
			    onCourse ? waypoint(destination.next) : target, clearance);

		if (!onCourse && !destination.arrived &&
		    glm::distance(agent.position, target) <= double{destination.arrivalRadius} &&
		    glm::distance(agent.velocity, targetVelocity) <= double{destination.arrivalSpeed})
		{
			destination.arrived = true;
			world.Events().Emit(Arrived{vehicle, destination.entity});
		}
		registry.replace<Physics::FlightControls>(
		    vehicle, Controls(
		                 transform.orientation, handling, velocity,
		                 destination.face && !onCourse ? &facing : nullptr));
	}
}
} // namespace Jettison::Ai
