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

struct Change3
{
	entt::entity entity;
	glm::dvec3 linear;
	glm::dvec3 angular;
};
} // namespace

void StepFlight(entt::registry &registry, double fixedStep)
{
	JETTISON_PROFILE_ZONE("Physics::StepFlight");
	std::vector<Change3> changes;
	const auto ships =
	    registry.view<const Thrusters, const FlightControls, const Sim::Transform, const Velocity>();
	for (const auto [entity, thrusters, controls, transform, velocity] : ships.each())
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
		    Capability(thrusters.lateral), Capability(thrusters.vertical),
		    Capability(thrusters.reverse)};
		const glm::dvec3 negative{
		    Capability(thrusters.lateral), Capability(thrusters.vertical),
		    Capability(thrusters.forward)};
		const double maxSpeed = Capability(thrusters.maxSpeed);

		glm::dvec3 linear{0.0};
		glm::dvec3 angular{0.0};
		for (glm::length_t axis = 0; axis < 3; ++axis)
		{
			linear[axis] = Change(
			    Demand(controls.linear[axis]), relative[axis], maxSpeed, positive[axis], negative[axis],
			    controls.assist, fixedStep);
			const double turn = Capability(thrusters.angularAcceleration[axis]);
			angular[axis] = Change(
			    Demand(controls.angular[axis]), spin[axis], Capability(thrusters.maxAngularRate[axis]),
			    turn, turn, controls.assist, fixedStep);
		}
		changes.push_back({entity, orientation * linear, orientation * angular});
	}

	for (const Change3 &change : changes)
	{
		Velocity &velocity = registry.get<Velocity>(change.entity);
		velocity.linear = glm::vec3{glm::dvec3{velocity.linear} + change.linear};
		velocity.angular = glm::vec3{glm::dvec3{velocity.angular} + change.angular};
	}
}
} // namespace Jettison::Physics
