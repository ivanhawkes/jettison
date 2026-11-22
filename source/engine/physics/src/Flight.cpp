#include <jettison/physics/Flight.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <jettison/physics/Components.h>
#include <jettison/profile/Profile.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Physics
{
namespace
{
// A demand, held to [-1, 1]; one that is not a number asks for nothing.
double Demand(float value)
{
	return std::isfinite(value) ? std::clamp(double{value}, -1.0, 1.0) : 0.0;
}


// A capability, none if it is not a positive number: a thruster cannot push the wrong way.
double Capability(float value)
{
	return value > 0.0f && std::isfinite(value) ? double{value} : 0.0;
}


// The change one axis makes over the step: towards the target at no more than the acceleration
// each way allows, without passing it, with assist; that share of the acceleration, without.
double Change(
    double demand, double current, double limit, double towardsPositive, double towardsNegative,
    bool assist, double step)
{
	if (!assist)
		return demand * (demand >= 0.0 ? towardsPositive : towardsNegative) * step;
	return std::clamp(demand * limit - current, -towardsNegative * step, towardsPositive * step);
}


// The share of an axis's thrust a change took, signed as the change is.
float Share(double change, double towardsPositive, double towardsNegative, double step)
{
	const double most = (change >= 0.0 ? towardsPositive : towardsNegative) * step;
	return most > 0.0 ? static_cast<float>(std::clamp(change / most, -1.0, 1.0)) : 0.0f;
}

struct Change3
{
	entt::entity entity;
	glm::dvec3 linear;
	glm::dvec3 angular;
	Thrust thrust;
};


Change3 ChangeOf(
    const entt::registry &registry, entt::entity entity, const Handling &handling,
    const FlightControls &controls, const Sim::Transform &transform, const Velocity &velocity,
    double fixedStep)
{
	{
		glm::dvec3 reference{0.0};
		if (registry.valid(controls.reference) && controls.reference != entity)
			if (const auto *moving = registry.try_get<Velocity>(controls.reference))
				reference = glm::dvec3{moving->linear};

		const glm::dquat orientation = glm::normalize(transform.orientation);
		const glm::dquat inverse = glm::conjugate(orientation);
		const glm::dvec3 relative = inverse * (glm::dvec3{velocity.linear} - reference);
		const glm::dvec3 spin = inverse * glm::dvec3{velocity.angular};

		// Each axis's thrust each way: starboard and port, up and down, astern (+Z) and ahead (-Z).
		const glm::dvec3 positive{
		    Capability(handling.lateral), Capability(handling.vertical), Capability(handling.reverse)};
		const glm::dvec3 negative{
		    Capability(handling.lateral), Capability(handling.vertical), Capability(handling.forward)};
		const double maxSpeed = Capability(handling.maxSpeed);

		glm::dvec3 linear{0.0};
		glm::dvec3 angular{0.0};
		Thrust thrust;
		for (glm::length_t axis = 0; axis < 3; ++axis)
		{
			linear[axis] = Change(
			    Demand(controls.linear[axis]), relative[axis], maxSpeed, positive[axis], negative[axis],
			    controls.assist, fixedStep);
			const double turn = Capability(handling.angularAcceleration[axis]);
			angular[axis] = Change(
			    Demand(controls.angular[axis]), spin[axis], Capability(handling.maxAngularRate[axis]),
			    turn, turn, controls.assist, fixedStep);
			thrust.linear[axis] = Share(linear[axis], positive[axis], negative[axis], fixedStep);
			thrust.angular[axis] = Share(angular[axis], turn, turn, fixedStep);
		}
		return {entity, orientation * linear, orientation * angular, thrust};
	}
}
} // namespace


void StepFlight(entt::registry &registry, double fixedStep)
{
	JETTISON_PROFILE_ZONE("Physics::StepFlight");
	std::vector<Change3> changes;
	const auto vehicles =
	    registry.view<const Handling, const FlightControls, const Sim::Transform, const Velocity>();
	for (const auto [entity, handling, controls, transform, velocity] : vehicles.each())
		changes.push_back(
		    ChangeOf(registry, entity, handling, controls, transform, velocity, fixedStep));

	// What no longer flies fires nothing.
	std::vector<entt::entity> grounded;
	for (const entt::entity entity : registry.view<const Thrust>())
		if (!vehicles.contains(entity))
			grounded.push_back(entity);
	registry.remove<Thrust>(grounded.begin(), grounded.end());

	for (const Change3 &change : changes)
	{
		Velocity &velocity = registry.get<Velocity>(change.entity);
		velocity.linear = glm::vec3{glm::dvec3{velocity.linear} + change.linear};
		velocity.angular = glm::vec3{glm::dvec3{velocity.angular} + change.angular};
		registry.emplace_or_replace<Thrust>(change.entity, change.thrust);
	}
}


bool FlightAtRest(const entt::registry &registry, double fixedStep)
{
	for (const auto [entity, handling, controls, transform, velocity] :
	     registry.view<const Handling, const FlightControls, const Sim::Transform, const Velocity>()
	         .each())
	{
		// Physics writes over the velocity of a kinematic body in a frame, and only of one in a frame.
		if (const auto *body = registry.try_get<Body>(entity);
		    body && body->kind == BodyKind::Kinematic && registry.all_of<Sim::InFrame>(entity) &&
		    registry.get<Sim::InFrame>(entity).frame != Sim::kNoFrame)
			continue;
		// A reference's velocity may change from step to step (a platform's on its orbit), and so may
		// the way rails turn the vehicle, and with either what it is held to.
		if ((registry.valid(controls.reference) && controls.reference != entity &&
		     registry.all_of<Velocity>(controls.reference)) ||
		    registry.all_of<OnRails>(entity))
			return false;
		const Change3 change =
		    ChangeOf(registry, entity, handling, controls, transform, velocity, fixedStep);
		if (change.linear != glm::dvec3{0.0} || change.angular != glm::dvec3{0.0})
			return false;
	}
	return true;
}
} // namespace Jettison::Physics
