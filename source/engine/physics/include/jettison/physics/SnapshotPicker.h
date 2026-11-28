#pragma once

#include <jettison/physics/Components.h>
#include <jettison/physics/Layer.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/physics/Shape.h>

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace Jettison::Sim
{
class DefinitionRegistry;
struct Snapshot;
} // namespace Jettison::Sim

namespace Jettison::Physics
{
// What a pick against the snapshots may hit.
struct PickOptions
{
	// The body the ray passes through: the one the camera is inside.
	entt::entity ignore{entt::null};

	// Static bodies, the ground and the buildings, picked too; left out, a pick or a hover finds only
	// what moves.
	bool statics{true};

	// Sensors are left out unless asked for, as a cast leaves them (CastOptions).
	bool sensors{false};

	// The layer the pick is cast as, as CastOptions::layer: it hits only what a moving body of that
	// layer would meet. None hits every layer.
	std::optional<std::string> layer{};
};

// A segment of a body's shape as Jolt draws it, in its frame's coordinates, and the body it is of.
using BodyLine = std::function<void(
    entt::entity entity, const Body &body, const glm::dvec3 &from, const glm::dvec3 &to)>;

// A pick presentation makes for itself (docs/physics.md, "Picking"): a ray against the bodies as the
// two newest snapshots draw them, each at the transform between them that the frame shows, so it is
// answered at once, while the world is paused, and every frame, for what the cursor is over. It reads
// no simulation state: the bodies (Body, which the world shows in its snapshots) and their places
// are the snapshots', and their shapes are definitions, Jolt's own, so it hits what the step's casts
// would. A pick the simulation answers and records is still the command Pick.
//
// shortcut: every body of the frame is tested, a ray against each shape; a broadphase over the
// snapshot is the step up should a frame hold tens of thousands of bodies.
class SnapshotPicker
{
  public:
	// With the shapes defined here, as a client with no physics world of its own defines them.
	SnapshotPicker();

	// With the physics world's shapes, by their definitions, as the shell has them; the world
	// outlives the picker.
	explicit SnapshotPicker(const PhysicsWorld &shapes);

	~SnapshotPicker();

	SnapshotPicker(const SnapshotPicker &) = delete;

	SnapshotPicker &operator=(const SnapshotPicker &) = delete;

	// Throws as PhysicsWorld::DefineShape does.
	void DefineShape(std::string name, const ShapeDefinition &shape);

	void DefineShapes(const Sim::DefinitionRegistry &definitions);

	// The game's layers, all at once, as a client with no physics world of its own defines them; a
	// picker made with a world reads the world's (PhysicsWorld::Layers). Throws as
	// PhysicsWorld::DefineLayers does, and std::logic_error for a picker with a world or with layers.
	void DefineLayers(const LayerDefinitions &layers);

	void DefineLayers(const Sim::DefinitionRegistry &definitions);

	// The first body a ray from the origin meets within the distance, in the frame, with each body
	// placed alpha of the way from previous to current (Sim::Interpolate), as the frame draws it.
	// Nothing for a ray of no direction or no length, and a body whose shape is not defined is passed
	// over. A ray from inside a convex body hits it at once, as a cast's does. Ties go to the lower
	// entity. Throws std::invalid_argument for a pick as a layer not defined, as a cast does.
	std::optional<CastHit> CastRay(
	    const Sim::Snapshot &previous, const Sim::Snapshot &current, double alpha, std::uint64_t frame,
	    const glm::dvec3 &origin, const glm::dvec3 &direction, double maxDistance,
	    const PickOptions &options = {}) const;

	// Every body of the frame drawn as Jolt draws its shape (docs/physics.md, "Seeing the bodies"),
	// in wireframe: each edge of its triangles, compound parts and offsets included, with each body
	// placed as CastRay places it. Jolt draws a round shape in less detail the farther it is from the
	// camera, in the frame. Sensors are drawn too; a body whose shape is not defined is passed over.
	// For presentation's dev tools: Jolt keeps one debug renderer in a process, which the first draw
	// makes and the picker keeps, so a second picker drawing while the first lives asserts in a Debug
	// tree.
	void DrawBodies(
	    const Sim::Snapshot &previous, const Sim::Snapshot &current, double alpha, std::uint64_t frame,
	    const glm::dvec3 &camera, const BodyLine &line) const;

  private:
	struct Impl;
	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Physics
