#pragma once

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

namespace Jettison::Sim
{
class SimWorld;

// Where a simulation entity is: a position in the system frame, in metres, and an orientation,
// both in double precision (docs/architecture.md, "Coordinate frames and units"). What physics
// will write and what presentation interpolates. Registered with Reflect, both fields Saved, by
// RegisterTypes.
struct Transform
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
};

// Registers the simulation's own types with Reflect. Idempotent; a world that holds Transforms
// calls it before RegisterComponent<Transform>().
void RegisterTypes();

struct TransformSample
{
	entt::entity entity{entt::null};
	Transform transform;
};

// The part of a world presentation may see, taken after a step: every entity with a Transform,
// sorted by identifier so a lookup is a search. Presentation reads snapshots, never the world.
struct Snapshot
{
	// The clock after the step: the steps run, and simulation seconds.
	std::uint64_t step{0};
	double time{0.0};
	std::vector<TransformSample> transforms;

	// Null if the entity has no Transform in this snapshot.
	const Transform *Find(entt::entity entity) const;
};

Snapshot Capture(const SimWorld &world);

// The entity's transform alpha of the way from previous to current: the position along the line,
// the orientation along the shorter arc. An entity only current has (made by the last step) is
// where current has it, with nothing to come from; one only previous has (destroyed by the last
// step) has no transform. An identifier carries its version, so an index the last step reused is
// a different entity, not a jump.
std::optional<Transform> Interpolate(
    const Snapshot &previous, const Snapshot &current, entt::entity entity, double alpha);
} // namespace Jettison::Sim
