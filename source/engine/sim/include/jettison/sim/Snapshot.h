#pragma once

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <any>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace Jettison::Serialise
{
class BinaryRecord;
template <typename Data> class Migrations;
} // namespace Jettison::Serialise

namespace Jettison::Sim
{
class SimWorld;

// Where a simulation entity is: a position in its frame (InFrame), in metres, and an orientation,
// both in double precision (docs/architecture.md, "Coordinate frames and units"). What physics
// will write and what presentation interpolates. Registered with Reflect, both fields Saved, by
// RegisterTypes.
struct Transform
{
	glm::dvec3 position{0.0};
	glm::dquat orientation{1.0, 0.0, 0.0, 0.0};
};

// Which frame an entity's Transform is in, for a world of more than one: a partition of the game's,
// each with its own origin, so two positions are comparable only in the same frame. A game of one
// frame gives its entities none. A frame is its name hashed as Core::StringId hashes it (FrameId),
// which a save keeps, so renaming one in the game's data moves its entities nowhere rather than into
// another frame. An entity between frames is in none (kNoFrame) until it arrives. The space sample's
// frames are its planetary systems, an entity passing through a portal in none. Registered with
// Reflect, its field Saved, by RegisterTypes; a world registers it with its chain (Migrations).
struct InFrame
{
	std::uint64_t frame{0};

	// Version 2 (A01) renamed the component, from "Sim.InSystem", and its field, from "system".
	static const Serialise::Migrations<Serialise::BinaryRecord> &Migrations();
};

inline constexpr std::uint64_t kNoFrame = 0;

// The identifier InFrame holds for the frame of that name.
std::uint64_t FrameId(std::string_view frameName);

// Registers the simulation's own types with Reflect. Idempotent; a world that holds Transforms
// calls it before RegisterComponent<Transform>() and RegisterComponent<InFrame>().
void RegisterTypes();

struct TransformSample
{
	entt::entity entity{entt::null};
	Transform transform;
	// The entity's InFrame, or kNoFrame for one that has none.
	std::uint64_t frame{kNoFrame};
};

// One entity's copy of a component the world shows (SimWorld::ShowInSnapshots).
template <typename T> struct ComponentSample
{
	entt::entity entity{entt::null};
	T value;
};

// The part of a world presentation may see, taken after a step: every entity with a Transform and
// the frame it is in, and every entity's copy of each component the world shows, each sorted by
// identifier so a lookup is a search. Presentation reads snapshots, never the world: a system scene
// shows the entities in its own system's frame (scene/SystemScene.h), and the UI's data models read
// what the game shows, a platform's market or a vehicle's hold (ui/DataModel.h).
struct Snapshot
{
	// The clock after the step: the steps run, and simulation seconds.
	std::uint64_t step{0};
	double time{0.0};
	std::vector<TransformSample> transforms;
	// By EnTT's type hash: a std::vector<ComponentSample<T>> for each component shown.
	std::map<entt::id_type, std::any> shown;

	// Null if the entity has no Transform in this snapshot.
	const Transform *Find(entt::entity entity) const;

	const TransformSample *FindSample(entt::entity entity) const;

	// Every entity's copy of a shown component, by identifier; empty for a type the world does not
	// show.
	template <typename T> std::span<const ComponentSample<T>> Components() const
	{
		const auto found = shown.find(entt::type_hash<T>::value());
		if (found == shown.end())
			return {};
		return std::any_cast<const std::vector<ComponentSample<T>> &>(found->second);
	}

	// The entity's copy, or null if it has none in this snapshot.
	template <typename T> const T *Get(entt::entity entity) const
	{
		const std::span<const ComponentSample<T>> samples = Components<T>();
		const auto found = std::ranges::lower_bound(
		    samples, entt::to_integral(entity), {},
		    [](const ComponentSample<T> &sample) { return entt::to_integral(sample.entity); });
		return found != samples.end() && found->entity == entity ? &found->value : nullptr;
	}
};

Snapshot Capture(const SimWorld &world);

// The entity's transform alpha of the way from previous to current: the position along the line,
// the orientation along the shorter arc. An entity only current has (made by the last step) is
// where current has it, with nothing to come from; one only previous has (destroyed by the last
// step) has no transform. An identifier carries its version, so an index the last step reused is
// a different entity, not a jump. An entity whose frame changed in the last step is where current
// has it too: the two positions are in different frames, and the line between them is
// nowhere.
std::optional<Transform> Interpolate(
    const Snapshot &previous, const Snapshot &current, entt::entity entity, double alpha);
} // namespace Jettison::Sim
