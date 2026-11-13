#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

#include <jettison/sim/Snapshot.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Ai::Detail
{
// A transit in whole steps, rounded up, and at least one: a lane's and a cruise's. A millionth of a
// step is let go before rounding up, since a transit authored in seconds is rarely a whole number of
// steps in floating point: 40 s at 60 Hz is 2400.0000000000005 steps.
inline std::uint64_t TransitSteps(double transitSeconds, double fixedStep)
{
	const double steps = std::ceil((transitSeconds / fixedStep) - 1.0e-6);
	return std::max<std::uint64_t>(1, steps > 0.0 ? static_cast<std::uint64_t>(steps) : 0);
}

// What a lane's or a cruise's arrival comes out clear of: each body in a system, where it is as the
// step ends, and the sphere that holds its shape.
struct Occupant
{
	std::uint64_t system{0};
	glm::dvec3 position{0.0};
	double radius{0.0};
};

// Every entity with a Physics::Body in one of the systems.
std::vector<Occupant> Occupants(
    const entt::registry &registry, const Physics::PhysicsWorld &physics,
    std::span<const std::uint64_t> systems);

// Where an arrival of that radius comes out in the system: at the place if it is clear of every
// occupant there by both radii and the clearance, or else at the first place clear on rings about
// it, in the plane across the way it faces (its x and y axes), each ring a hull's breadth and the
// clearance further out, with six more places round each than the last.
glm::dvec3 ClearPlace(
    std::span<const Occupant> occupants, std::uint64_t system, const Sim::Transform &place,
    double radius, double clearance);
} // namespace Jettison::Ai::Detail
