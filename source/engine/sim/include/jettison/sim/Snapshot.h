#pragma once

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
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

// Which star system an entity is in, so whose frame its Transform is in: the simulation is the
// whole galaxy's, and every system's positions are metres from that system's primary. The system is
// its name hashed as Core::StringId hashes it (SystemId), which a save keeps, so renaming a system
// in the galaxy file moves its entities nowhere rather than into another system. An entity in
// transit along a jump lane is in none (kNoSystem) until it arrives. Registered with Reflect, its
// field Saved, by RegisterTypes.
struct InSystem
{
	std::uint64_t system{0};
};

inline constexpr std::uint64_t kNoSystem = 0;

// The identifier InSystem holds for the system of that name.
std::uint64_t SystemId(std::string_view systemName);

// Registers the simulation's own types with Reflect. Idempotent; a world that holds Transforms
// calls it before RegisterComponent<Transform>() and RegisterComponent<InSystem>().
void RegisterTypes();

struct TransformSample
{
	entt::entity entity{entt::null};
	Transform transform;
	// The entity's InSystem, or kNoSystem for one that has none.
	std::uint64_t system{kNoSystem};
};

// The part of a world presentation may see, taken after a step: every entity with a Transform and
// the system it is in, sorted by identifier so a lookup is a search. Presentation reads snapshots,
// never the world; a system scene shows the entities in its own system (scene/SystemScene.h).
struct Snapshot
{
	// The clock after the step: the steps run, and simulation seconds.
	std::uint64_t step{0};
	double time{0.0};
	std::vector<TransformSample> transforms;

	// Null if the entity has no Transform in this snapshot.
	const Transform *Find(entt::entity entity) const;
	const TransformSample *FindSample(entt::entity entity) const;
};

Snapshot Capture(const SimWorld &world);

// The entity's transform alpha of the way from previous to current: the position along the line,
// the orientation along the shorter arc. An entity only current has (made by the last step) is
// where current has it, with nothing to come from; one only previous has (destroyed by the last
// step) has no transform. An identifier carries its version, so an index the last step reused is
// a different entity, not a jump. An entity whose system changed in the last step is where current
// has it too: the two positions are in different systems' frames, and the line between them is
// nowhere.
std::optional<Transform> Interpolate(
    const Snapshot &previous, const Snapshot &current, entt::entity entity, double alpha);
} // namespace Jettison::Sim
