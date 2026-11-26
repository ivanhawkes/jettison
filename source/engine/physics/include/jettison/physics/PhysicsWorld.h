#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/physics/Components.h>
#include <jettison/physics/Shape.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Sim
{
class DefinitionRegistry;
}

namespace Jettison::Physics
{
// Jolt's capacities for one frame's bodies (Sim::InFrame), fixed when that frame's physics is made.
struct PhysicsSettings
{
	std::uint32_t maxBodies{8192};
	std::uint32_t maxBodyPairs{32768};
	std::uint32_t maxContactConstraints{16384};

	// The fastest a body moves in its frame. Jolt's default, 500 m/s, is far below the 7.7 km/s
	// a game's platform may go at, and a kinematic body following one would be held to it.
	float maxLinearVelocity{1.0e5f};
};

// Two bodies beginning or ending to touch (PhysicsWorld::Contacts), or a body and a sensor beginning
// or ending to overlap.
enum class ContactPhase : std::uint8_t
{
	Began,
	Ended
};

// What one step changed about two bodies' contact. The pair is in entity order, first below second.
// A beginning says where they met and how hard: the point in the frame, the normal from the
// first towards the second, and the speed they closed at along it as they met, before physics
// pushed them apart, which is what an impact's sound or damage is judged by. An end carries none of
// it. Registered with Reflect, every field Saved, by RegisterTypes.
struct ContactEvent
{
	ContactPhase phase{ContactPhase::Began};
	entt::entity first{entt::null};
	entt::entity second{entt::null};

	// One of them is a sensor, so nothing was pushed.
	bool sensor{false};
	glm::dvec3 point{0.0};
	glm::vec3 normal{0.0f};
	float speed{0.0f};

	bool operator==(const ContactEvent &) const = default;
};

// What a cast found: the entity, how far along the cast in metres, and where, in the frame,
// with the surface's normal there, facing back along the cast.
struct CastHit
{
	entt::entity entity{entt::null};
	double distance{0.0};
	glm::dvec3 point{0.0};
	glm::vec3 normal{0.0f};
};

// What a cast may hit, and when it is cast.
struct CastOptions
{
	// The caster's own body, which a ray from inside it would otherwise hit at once.
	entt::entity ignore{entt::null};

	// Sensors are left out unless they are asked for: a ray picking a platform hits its shape, not the
	// docking zone before it.
	bool sensors{false};

	// The simulation time to place each frame at, rather than where the last step left it, with its
	// bodies where they are in it. Presentation shows the world between the last two steps
	// (Sim::Stepper), and a platform's frame may move 500 m a step, so a ray drawn from
	// what is shown is cast at the time shown, or it misses the platform by the distance between them.
	std::optional<double> framesAt;
};

// The command that picks what is under the mouse (docs/physics.md): a ray, drawn by
// presentation from what it shows, cast against physics as the step applying it begins and answered
// with a Picked event, so presentation never reads the simulation's state. The request is the
// caller's, handed back in the answer with its sender, so each player's presentation takes its own
// answers. Registered with Reflect, every field Saved, by RegisterTypes.
struct Pick
{
	Sim::Controller sender;
	std::uint64_t request{0};

	// The frame cast in (Sim::InFrame).
	std::uint64_t frame{0};
	glm::dvec3 origin{0.0};
	glm::dvec3 direction{0.0, 0.0, -1.0};
	double maxDistance{0.0};

	// The simulation time the ray was drawn at (CastOptions::framesAt).
	double time{0.0};
	entt::entity ignore{entt::null};
};

// The answer to a Pick, raised as a Sim::EventQueue event: what the ray hit first, or a null entity
// for nothing, as there is for a ray of no direction or no length. Registered with Reflect, every
// field Saved, by RegisterTypes.
struct Picked
{
	Sim::Controller sender;
	std::uint64_t request{0};
	entt::entity entity{entt::null};
	double distance{0.0};
	glm::dvec3 point{0.0};
	glm::vec3 normal{0.0f};
};

// The Jolt a save's physics state was written by: its version, and those of the build's feature bits
// (Jolt's JPH_VERSION_FEATURES) that change what Jolt saves or how it steps, double precision, the
// cross-platform determinism and the width of an object layer. The others (assertions, the
// profiler, the debug renderer) differ between a Debug and a Release tree and change neither, so a
// save moves between them with Jolt's state kept.
struct JoltVersion
{
	std::uint32_t major{0};
	std::uint32_t minor{0};
	std::uint32_t patch{0};
	std::uint64_t features{0};

	bool operator==(const JoltVersion &) const = default;
};

// This build's: what a save is written as, and a load keeps Jolt's state for.
JoltVersion BuiltJoltVersion();

// What a frame holds besides its bodies (PhysicsWorld::DefineFrame): the bodies of the game's that
// rails and local frames name (OnRails, LocalFrame), each where the game's rules put it at a time, and
// the frame's gravity. A definition, not state: neither saved nor hashed, so it gives the same answer
// for the same time in every run. Called on the simulation's thread, during a step and from the
// queries that take a frame.
class FrameModel
{
  public:
	virtual ~FrameModel() = default;

	// Whether the frame has a body of that name.
	virtual bool HasBody(std::string_view body) const = 0;

	// Where the body is at a time, in the frame, and how it is turned. Only asked of a body the frame
	// has (HasBody).
	virtual Sim::Transform BodyAt(std::string_view body, double simSeconds) const = 0;

	// The acceleration gravity gives at a time, in metres a second squared, at each position in the
	// frame: what a step adds to each dynamic body, at where the body is as the step begins. Asked
	// once a step; none, an empty function, for a frame without gravity, as it is unless overridden.
	virtual std::function<glm::dvec3(const glm::dvec3 &)> GravityAt(double simSeconds) const;
};

// Jolt in double precision, stepped inside the simulation's fixed step (docs/architecture.md,
// "Simulation and presentation"). Its state is the simulation's: the world owns it as a service
// (Sim::WorldService), a save holds its bodies and their contacts, and a world loaded from a save
// steps on exactly as the one saved did. Jolt's types stay inside this module; the engine sees
// bodies as entities with a Body, a Sim::Transform and a Sim::InFrame (Components.h).
//
// Each frame with bodies (Sim::InFrame), and each local frame in it (LocalFrame), is a Jolt world of
// its own, made when the first body enters it and gone when the last leaves: two frames' bodies at
// the same coordinates are nowhere near each other, and in a local frame Jolt's coordinates are
// metres from the frame's body and its velocities relative to the body's motion, which keeps Jolt's
// floats and its sweeps on the small numbers that separate a vehicle from a platform. A body that
// changes frame or local frame is made again in the other's.
//
// What moves a frame's named bodies, and what pulls in it, is the game's (FrameModel, DefineFrame),
// its start-up as shapes are: what puts an entity on rails (OnRails) and carries a local frame, and
// the frame's gravity, which moves only dynamic bodies. A frame with no model has no rails, no local
// frames and no gravity. Rails a frame's model moves stay on them under its gravity, so a vehicle
// beside a platform the model carries falls with it rather than away from it.
//
// One fixed step, as the simulation system "physics" (kSystemName), after the flight model
// ("flight", Flight.h), which a game's systems order themselves against with `after`:
//   1. Entities on rails are put where their bodies are at the time the step ends.
//   2. Bodies are made for entities that gained what makes one, and removed for those that lost it
//      or whose Body changed, which are made again. Every entity on rails and every body to be made
//      is checked first, and a step that cannot place or make one throws std::invalid_argument
//      naming the entity before it changes anything (SimWorld::Step says what a throwing step
//      leaves). A kinematic body on rails is made where its body was as the step began, so the
//      step moves it with its orbit's velocity from the first.
//   3. What the game wrote is applied: a static or dynamic body whose Transform was written is
//      placed there, a dynamic body whose Velocity was written takes it, and every kinematic body
//      is moved towards its Transform over the step; in a world with nothing dynamic it is put
//      there instead, at rest and asleep, its Velocity what the move implies, so its state is its
//      place alone (docs/simulation.md, "Quiet steps and fast-forward"). Then each dynamic body in a
//      defined frame gains what its frame's gravity gives it over the step, from where the bodies are
//      as the step begins.
//   4. Each frame's worlds step, in identifier order, on one thread.
//   5. Each moving body's Velocity, and each dynamic body's Transform, are written from it.
//   6. The contacts the step began and ended are what Contacts() returns until the next.
// Bodies are visited in entity order throughout, so the same state and commands give the same
// calls into Jolt, and with Jolt built CROSS_PLATFORM_DETERMINISTIC, the same results in every
// build.
//
// Contacts are the simulation's to read (docs/physics.md): a game system ordered
// after "physics" reads the step's, and one before it reads the last step's, which a save holds,
// so a world loaded from it hands the same contacts to the same systems. Two bodies touch while
// Jolt has a contact between them, which begins a couple of centimetres before they meet, as
// Jolt's speculative contacts do; a body touches a sensor while it overlaps it. A body asleep on
// another, or on a static body, still touches it, though Jolt drops a sleeping body's contacts.
// A body removed, or made again (a change of Body, frame or local frame), ends every contact it had in
// the step that removes it, and one made again begins its own afresh.
class PhysicsWorld final : public Sim::WorldService
{
  public:
	static constexpr std::string_view kSystemName = "physics";
	static constexpr std::string_view kServiceName = "physics";

	// Makes a physics world and gives it to the simulation world, which owns it from then on:
	// registers Body, Velocity, OnRails, Handling and FlightControls as components, the
	// SetFlightControls and Pick commands, the service, and the "flight" and "physics" systems, and
	// shows Thrust (Flight.h) in the snapshots. The game registers Sim::Transform and Sim::InFrame
	// itself, as it always does. Throws std::logic_error as SimWorld's registrations do, for one
	// already made.
	static PhysicsWorld &Attach(Sim::SimWorld &world, const PhysicsSettings &settings = {});

	~PhysicsWorld() override;

	// A shape bodies can name. Built here, so a shape Jolt refuses throws std::invalid_argument
	// naming it, as does a name already defined. Definitions are the game's start-up, the same
	// every run: a save names shapes, and a world it is loaded into has to define them.
	void DefineShape(std::string name, const ShapeDefinition &shape);

	// Every ShapeDefinition the registry has loaded, under its definition name.
	void DefineShapes(const Sim::DefinitionRegistry &definitions);

	bool HasShape(std::string_view name) const;

	// The definition a shape was defined by, or null for a name not defined: what a picker over the
	// snapshots builds its own from (SnapshotPicker), a definition and not the step's state.
	const ShapeDefinition *FindShapeDefinition(std::string_view name) const;

	// The radius of the smallest sphere about the body's origin holding the shape: a box's half
	// diagonal, a mesh's farthest vertex. What steering keeps clear of a body. Throws
	// std::invalid_argument for a shape that is not defined.
	double ShapeRadius(std::string_view name) const;

	// The largest ShapeRadius of any shape defined: how far beyond a point a body's shape can reach,
	// which a query for what is near widens itself by. None while no shape is defined.
	double LargestShapeRadius() const;

	// The model of the frame of that identifier (Sim::InFrame). Throws std::invalid_argument for no
	// model, or a frame already defined.
	void DefineFrame(std::uint64_t frame, std::unique_ptr<const FrameModel> model);

	bool HasFrame(std::uint64_t frame) const;

	// The acceleration the defined frame's gravity gives a body at a position at a time, in metres a
	// second squared (FrameModel::GravityAt). None in a frame that is not defined or has no gravity.
	glm::dvec3 Gravity(std::uint64_t frame, const glm::dvec3 &position, double simSeconds) const;

	// Where a local frame (LocalFrame) is at a time, in the frame: its body's position, or none for
	// the frame's own (an empty name). A point a vehicle is flown to in its local frame is this far
	// from the frame's. Throws std::invalid_argument for a local frame a body could not be in.
	glm::dvec3 FrameOrigin(std::uint64_t frame, std::string_view local, double simSeconds) const;

	// The simulation seconds every entity on rails and every body is placed for: the end of the last
	// step or skip, so a step's systems after physics see a step on from the clock's Now and those
	// before it see Now. None before the first after this is made or loaded, when they are placed for
	// Now. What measures a Transform against a body's place, as its frame's model gives it, evaluates
	// the body at this time.
	std::optional<double> PlacedAt() const;

	// Where an entity on those rails in the defined frame is at a time, as each step puts it: what a
	// game places an entity at before the first step that moves it, so nothing reads it elsewhere in
	// between. Throws std::invalid_argument for a frame not defined or a body it does not have.
	Sim::Transform RailsPlace(std::uint64_t frame, const OnRails &rails, double simSeconds) const;

	std::size_t BodyCount() const;

	std::size_t BodyCount(std::uint64_t frame) const;

	// The local frames of a frame that have bodies, its own among them: each is a Jolt world
	// (LocalFrame).
	std::size_t FrameCount(std::uint64_t frame) const;

	// The frames that have bodies, in identifier order.
	std::vector<std::uint64_t> Frames() const;

	// The contacts the last step began and ended, in order of pair, and for one pair an end before
	// a beginning (a body made again).
	const std::vector<ContactEvent> &Contacts() const;

	// Whether the entity's body sleeps: a moving body still for half a second is no longer stepped
	// until something meets or moves it. A static body, and an entity with none, is not asleep.
	bool Asleep(entt::entity entity) const;

	// What touches the entity's body now, in entity order: none for an entity with no body.
	std::vector<entt::entity> Touching(entt::entity entity) const;

	// The first body a ray from the origin meets within the distance, across every local frame of
	// the frame, as the last step left them. A ray from inside a convex body hits it at once, and one
	// from inside a mesh, which has no inside, passes out through it. Throws std::invalid_argument
	// for a direction of no length or a distance that is not positive and finite.
	std::optional<CastHit> CastRay(
	    std::uint64_t frame, const glm::dvec3 &origin, const glm::dvec3 &direction, double maxDistance,
	    const CastOptions &options = {}) const;

	// The first body the shape meets swept from where it starts along the direction, within the
	// distance: whether a vehicle fits through a gap, or how far it can go before it meets the
	// platform. A shape that starts overlapping a body meets it at once. Throws std::invalid_argument
	// as CastRay does, and for a shape that is not defined.
	std::optional<CastHit> CastShape(
	    std::uint64_t frame, std::string_view shape, const Sim::Transform &start,
	    const glm::dvec3 &direction, double maxDistance, const CastOptions &options = {}) const;

	// Every body a ray from the origin meets within the distance, across every local frame of the
	// frame, each once at its nearest hit, nearest first and a tie by entity: the first is what
	// CastRay returns. What AI and triggers ask for "everything along this ray". Throws
	// std::invalid_argument as CastRay does.
	std::vector<CastHit> CastRayAll(
	    std::uint64_t frame, const glm::dvec3 &origin, const glm::dvec3 &direction, double maxDistance,
	    const CastOptions &options = {}) const;

	// Every body the shape meets swept as CastShape sweeps it, each once at its first contact,
	// nearest first and a tie by entity. Throws std::invalid_argument as CastShape does.
	std::vector<CastHit> CastShapeAll(
	    std::uint64_t frame, std::string_view shape, const Sim::Transform &start,
	    const glm::dvec3 &direction, double maxDistance, const CastOptions &options = {}) const;

	// Every body the defined shape, placed there, overlaps, across every local frame of the frame,
	// each once in entity order: what is in a trigger's volume. Throws std::invalid_argument for a
	// shape that is not defined or is a mesh, which Jolt does not collide as the moving shape, and
	// for a position that is not finite.
	std::vector<entt::entity> Overlap(
	    std::uint64_t frame, std::string_view shape, const Sim::Transform &at,
	    const CastOptions &options = {}) const;

	// The same for a sphere of the radius about the centre, with no shape defined for it. Throws
	// std::invalid_argument for a radius that is not positive and finite, or a centre that is not.
	std::vector<entt::entity> OverlapSphere(
	    std::uint64_t frame, const glm::dvec3 &centre, double radius,
	    const CastOptions &options = {}) const;

	// Every frame's bodies, each with what it was made from and was last placed at, and Jolt's own
	// state, under its version: positions, velocities, sleep, and the contacts each step starts
	// from; and the contacts as the engine sees them, what touches and what the last step began and
	// ended.
	void Save(Serialise::BinaryWriter &writer) const override;

	// A save of this build's Jolt is loaded as it was saved, and steps on exactly as the world saved
	// would have. One of another Jolt, whose state this build cannot read, is loaded by rebuilding
	// its bodies from physics' records instead, each where it was placed, moving as it was, and
	// asleep if it was: the world steps on, but not as the one saved would have, without the
	// contacts Jolt warm-starts each step from, and with each awake body's time at rest begun again
	// (docs/persistence.md). What touches is kept, and the contacts Jolt finds
	// again in the first step begin nothing.
	std::function<void()> Load(
	    Serialise::BinaryReader &reader, const entt::registry &loaded,
	    std::span<const entt::meta_any> queued) override;

	// What Save writes as Jolt's version, in place of this build's: a save that a load rebuilds, as
	// it would one of a Jolt upgrade, which one build's single Jolt cannot otherwise give a test. The
	// state saved is still this build's Jolt's, and the load does not read it.
	void SaveAsJoltVersion(const JoltVersion &version);

  private:
	struct Impl;

	explicit PhysicsWorld(const PhysicsSettings &settings);

	// The step from simSeconds to simSeconds + fixedStep.
	void Step(entt::registry &registry, double simSeconds, double fixedStep);

	// The step physics has to run in next (Sim::Quiet): the one about to begin, unless it has rested
	// for two (nothing dynamic, nothing awake, no body made or removed) and the registry asks for no
	// body to be made, removed or placed, every kinematic one being on rails and every body on rails
	// where its rails put it now (a system after physics may have changed them). Then never: steps
	// pass it by until something dynamic is in a world.
	std::uint64_t QuietFrom(const entt::registry &registry, std::uint64_t step, double now) const;

	// Puts what the steps passed over would have left (Sim::Quiet): each entity on rails where its
	// body is at now, each kinematic body there in Jolt with the velocity its last step's motion
	// implies, and each local frame where its body is, moving as over that step, which began at
	// lastBegan.
	void Skip(entt::registry &registry, double lastBegan, double now, double fixedStep);

	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Physics
