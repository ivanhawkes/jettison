#include <jettison/ai/Steering.h>

#include <jettison/ai/Types.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/profile/Profile.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/spatial/SystemIndices.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
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
// The share of a ship's weakest thruster it brakes with, and of its angular acceleration it stops
// a turn with: the rest is for turning and avoiding while it does.
constexpr double kBrakingShare = 0.5;
constexpr double kTurnShare = 0.5;
// Below this speed a ship holds its heading rather than turn to a velocity that is mostly the
// last few centimetres of an arrival.
constexpr double kFacingSpeed = 1.0;

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

// The controls that fly the velocity: with assist, a share of maxSpeed along each of the ship's
// axes, and a turn of the nose (-Z) towards it about the axis square to both, at the rate the turn
// can still be stopped from.
Physics::FlightControls Controls(
    const glm::dquat &orientation, const Physics::Thrusters &thrusters, const glm::dvec3 &velocity)
{
	Physics::FlightControls controls;
	controls.assist = true;
	const glm::dquat inverse = glm::conjugate(glm::normalize(orientation));
	if (const double maxSpeed = Capability(thrusters.maxSpeed); maxSpeed > 0.0)
		controls.linear = glm::vec3{glm::clamp(inverse * velocity / maxSpeed, -1.0, 1.0)};

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

	const double maxRate =
	    std::min(Capability(thrusters.maxAngularRate.x), Capability(thrusters.maxAngularRate.y));
	const double acceleration = kTurnShare * std::min(
	                                             Capability(thrusters.angularAcceleration.x),
	                                             Capability(thrusters.angularAcceleration.y));
	const glm::dvec3 turn = axis * std::min(maxRate, std::sqrt(2.0 * acceleration * angle));
	for (glm::length_t index = 0; index < 2; ++index)
	{
		const double rate = Capability(thrusters.maxAngularRate[index]);
		controls.angular[index] =
		    rate > 0.0 ? static_cast<float>(std::clamp(turn[index] / rate, -1.0, 1.0)) : 0.0f;
	}
	return controls;
}

std::string_view FrameOf(const entt::registry &registry, entt::entity entity)
{
	const auto *frame = registry.try_get<Physics::LocalFrame>(entity);
	return frame ? std::string_view{frame->body} : std::string_view{};
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
    Sim::SimWorld &world, const Physics::PhysicsWorld &physics, const Spatial::SystemIndices &indices)
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
		    if (!registry.valid(command.ship))
			    return;
		    Destination destination = command.destination;
		    destination.next = 0;
		    destination.arrived = false;
		    registry.emplace_or_replace<Destination>(command.ship, std::move(destination));
	    });
	world.RegisterCommand<ClearDestination>(
	    [](Sim::SimWorld &target, const ClearDestination &command)
	    {
		    entt::registry &registry = target.Registry();
		    if (!registry.valid(command.ship))
			    return;
		    registry.remove<Destination>(command.ship);
		    if (auto *controls = registry.try_get<Physics::FlightControls>(command.ship))
			    *controls = Physics::FlightControls{};
	    });
	Ecs::SystemDescription system{
	    std::string{kSystemName},
	    Ecs::TickPhase::Simulation,
	    Ecs::Access::Of<
	        Steered, Physics::Thrusters, Physics::Velocity, Physics::Body, Physics::LocalFrame,
	        Sim::Transform, Sim::InSystem>()
	        .Writing<Destination, Physics::FlightControls>(),
	    {std::string{Spatial::SystemIndices::kSystemName}},
	    [steering, &world](entt::registry & /*registry*/, double /*fixedStep*/)
	    { steering->Step(world); }};
	system.before = {std::string{Physics::kFlightSystemName}};
	world.RegisterSystem(std::move(system));
	return *steering;
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
	std::vector<entt::entity> ships;
	for (const entt::entity ship :
	     registry.view<
	         const Steered, Destination, const Physics::Thrusters, const Physics::FlightControls,
	         const Physics::Velocity, const Physics::Body, const Sim::Transform, const Sim::InSystem>())
		ships.push_back(ship);
	std::ranges::sort(ships, {}, [](entt::entity ship) { return entt::to_integral(ship); });

	// Each frame's origin as the step begins, as it is asked for.
	std::map<std::pair<std::uint64_t, std::string>, glm::dvec3> origins;
	const auto originOf = [&](std::uint64_t system, std::string_view frame) -> const glm::dvec3 &
	{
		std::pair<std::uint64_t, std::string> key{system, std::string{frame}};
		auto found = origins.find(key);
		if (found == origins.end())
			found = origins.emplace(std::move(key), m_physics.FrameOrigin(system, frame, now)).first;
		return found->second;
	};

	const double largest = m_physics.LargestShapeRadius();
	std::vector<entt::entity> near;
	std::vector<Obstacle> obstacles;
	std::vector<Obstacle> neighbours;
	for (const entt::entity ship : ships)
	{
		const std::uint64_t system = registry.get<const Sim::InSystem>(ship).system;
		if (system == Sim::kNoSystem)
			continue;
		const Steered &steered = registry.get<const Steered>(ship);
		Destination &destination = registry.get<Destination>(ship);
		const Physics::Thrusters &thrusters = registry.get<const Physics::Thrusters>(ship);
		const Sim::Transform &transform = registry.get<const Sim::Transform>(ship);
		const std::string_view frame = FrameOf(registry, ship);
		const glm::dvec3 &origin = originOf(system, frame);

		// Where the destination is, and how it moves.
		glm::dvec3 target = origin + destination.offset;
		glm::dvec3 targetVelocity{0.0};
		if (destination.entity != entt::null)
		{
			const entt::entity entity = destination.entity;
			const char *lost = nullptr;
			if (!registry.valid(entity) || !registry.all_of<Sim::Transform, Sim::InSystem>(entity))
				lost = "its destination has gone";
			else if (registry.get<const Sim::InSystem>(entity).system != system)
				lost = "its destination is in another system";
			else if (FrameOf(registry, entity) != frame)
				lost = "its destination is in another frame";
			if (lost)
			{
				registry.remove<Destination>(ship);
				registry.replace<Physics::FlightControls>(ship);
				world.Events().Emit(DestinationLost{ship, lost});
				continue;
			}
			const Sim::Transform &at = registry.get<const Sim::Transform>(entity);
			const glm::dvec3 offset = at.orientation * destination.offset;
			target = at.position + offset;
			if (const auto *moving = registry.try_get<Physics::Velocity>(entity))
				targetVelocity =
				    glm::dvec3{moving->linear} + glm::cross(glm::dvec3{moving->angular}, offset);
		}
		const auto waypoint = [&](std::uint32_t index)
		{ return origin + destination.waypoints[index]; };
		while (destination.next < destination.waypoints.size() &&
		       glm::distance(transform.position, waypoint(destination.next)) <=
		           double{destination.passRadius})
			++destination.next;

		const double maxSpeed =
		    destination.speed > 0.0f
		        ? std::min(double{destination.speed}, Capability(thrusters.maxSpeed))
		        : Capability(thrusters.maxSpeed);
		const Agent agent{
		    transform.position, glm::dvec3{registry.get<const Physics::Velocity>(ship).linear},
		    maxSpeed,
		    kBrakingShare * std::min(
		                        {Capability(thrusters.reverse), Capability(thrusters.lateral),
		                         Capability(thrusters.vertical)}),
		    Radius(registry.get<const Physics::Body>(ship).shape)};

		// What is near, in its frame.
		obstacles.clear();
		neighbours.clear();
		const double lookahead = std::max(0.0, double{steered.lookahead});
		const double clearance = std::max(0.0, double{steered.clearance});
		const double separation = std::max(0.0, double{steered.separation});
		if (const Spatial::SpatialIndex *index = m_indices.Find(system))
		{
			const double reach =
			    lookahead * 2.0 * maxSpeed + agent.radius + largest + std::max(clearance, separation);
			index->QuerySphere(agent.position, reach, near);
			for (const entt::entity entity : near)
			{
				const auto *body = registry.try_get<Physics::Body>(entity);
				if (entity == ship || !body || body->sensor || FrameOf(registry, entity) != frame)
					continue;
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
		const glm::dvec3 desired =
		    onCourse ? Seek(agent, waypoint(destination.next)) : Arrive(agent, target, targetVelocity);
		const glm::dvec3 velocity = HeldTo(
		    Avoid(agent, desired, obstacles, lookahead, clearance) +
		        Separate(agent, neighbours, separation),
		    maxSpeed);

		if (!onCourse && !destination.arrived &&
		    glm::distance(agent.position, target) <= double{destination.arrivalRadius} &&
		    glm::distance(agent.velocity, targetVelocity) <= double{destination.arrivalSpeed})
		{
			destination.arrived = true;
			world.Events().Emit(Arrived{ship, destination.entity});
		}
		registry.replace<Physics::FlightControls>(
		    ship, Controls(transform.orientation, thrusters, velocity));
	}
}
} // namespace Jettison::Ai
