#include "Transit.h"

#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Jettison::Ai::Detail
{
std::vector<Occupant> Occupants(
    const entt::registry &registry, const Physics::PhysicsWorld &physics,
    std::span<const std::uint64_t> systems)
{
	std::vector<Occupant> occupants;
	for (const auto [entity, body, inSystem, transform] :
	     registry.view<const Physics::Body, const Sim::InSystem, const Sim::Transform>().each())
		if (!body.sensor && std::ranges::find(systems, inSystem.system) != systems.end())
			occupants.push_back({inSystem.system, transform.position, physics.ShapeRadius(body.shape)});
	return occupants;
}

glm::dvec3 ClearPlace(
    std::span<const Occupant> occupants, std::uint64_t system, const Sim::Transform &place,
    double radius, double clearance)
{
	const auto clear = [&](const glm::dvec3 &at)
	{
		return std::ranges::none_of(
		    occupants,
		    [&](const Occupant &occupant)
		    {
			    return occupant.system == system &&
			           glm::distance(occupant.position, at) < occupant.radius + radius + clearance;
		    });
	};
	if (clear(place.position))
		return place.position;
	// Six places on the first ring, twelve on the second and so on.
	const double spacing = 2.0 * radius + clearance;
	const glm::dvec3 right = place.orientation * glm::dvec3{1.0, 0.0, 0.0};
	const glm::dvec3 up = place.orientation * glm::dvec3{0.0, 1.0, 0.0};
	for (int ring = 1;; ++ring)
		for (int slot = 0; slot < 6 * ring; ++slot)
		{
			const double angle =
			    glm::two_pi<double>() * static_cast<double>(slot) / static_cast<double>(6 * ring);
			const glm::dvec3 at = place.position + (right * std::cos(angle) + up * std::sin(angle)) *
			                                           (spacing * static_cast<double>(ring));
			if (clear(at))
				return at;
		}
}
} // namespace Jettison::Ai::Detail
