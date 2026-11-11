#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/celestial/StarSystem.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/Shape.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Sim
{
class DefinitionRegistry;
}

namespace Jettison::Physics
{
// Jolt's capacities for one star system's bodies, fixed when that system's physics is made.
struct PhysicsSettings
{
	std::uint32_t maxBodies{8192};
	std::uint32_t maxBodyPairs{32768};
	std::uint32_t maxContactConstraints{16384};
	// The fastest a body moves in its system's frame. Jolt's default, 500 m/s, is far below a
	// station's 7.7 km/s on its orbit, and a kinematic body following one would be held to it.
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
// A beginning says where they met and how hard: the point in the system frame, the normal from the
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

// What a cast found: the entity, how far along the cast in metres, and where, in the system frame,
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
	// Sensors are left out unless they are asked for: a ray picking a station hits its hull, not the
	// docking zone before it.
	bool sensors{false};
	// The simulation time to place each frame at, rather than where the last step left it, with its
	// bodies where they are in it. Presentation shows the world between the last two steps
	// (Sim::Stepper), and a station's frame moves 500 m a step about the star, so a ray drawn from
	// what is shown is cast at the time shown, or it misses the station by the distance between them.
	std::optional<double> framesAt;
};

// The command that picks what is under the mouse (docs/architecture.md, "Physics"): a ray, drawn by
// presentation from what it shows, cast against physics as the step applying it begins and answered
// with a Picked event, so presentation never reads the simulation's state. The request is the
// caller's, handed back in the answer. Registered with Reflect, every field Saved, by RegisterTypes.
struct Pick
{
	std::uint64_t request{0};
	std::uint64_t system{0};
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
	std::uint64_t request{0};
	entt::entity entity{entt::null};
	double distance{0.0};
	glm::dvec3 point{0.0};
	glm::vec3 normal{0.0f};
};

// Jolt in double precision, stepped inside the simulation's fixed step (docs/architecture.md,
// "Simulation and presentation"). Its state is the simulation's: the world owns it as a service
// (Sim::WorldService), a save holds its bodies and their contacts, and a world loaded from a save
// steps on exactly as the one saved did. Jolt's types stay inside this module; the engine sees
// bodies as entities with a Body, a Sim::Transform and a Sim::InSystem (Components.h).
//
// Each frame of each star system with bodies is a Jolt world of its own (LocalFrame), made when the
// first body enters it and gone when the last leaves: every system's positions are metres from its
// own primary, so two systems' bodies at the same coordinates are nowhere near each other, and in a
// frame on rails Jolt's coordinates are metres from the frame's body and its velocities relative to
// the body's motion, which keeps Jolt's floats and its sweeps on the small numbers that separate a
// ship from a station. A body that changes system or frame is made again in the other's.
//
// Star systems are definitions too (DefineStarSystem), the game's start-up as shapes are: what puts
// an entity on rails (OnRails) where its celestial body is, and the gravity of a system's bodies.
// Orbits stay on rails (celestial), so gravity moves only dynamic bodies, and only in a defined
// system: a ship beside a station falls round the planet with it, as the station's rails carry it,
// rather than falling away from it at the planet's pull.
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
//      is moved towards its Transform over the step. Then each dynamic body in a defined system
//      gains what its system's gravity gives it over the step, from where the bodies are as the
//      step begins.
//   4. Each system's world steps, in identifier order, on one thread.
//   5. Each moving body's Velocity, and each dynamic body's Transform, are written from it.
//   6. The contacts the step began and ended are what Contacts() returns until the next.
// Bodies are visited in entity order throughout, so the same state and commands give the same
// calls into Jolt, and with Jolt built CROSS_PLATFORM_DETERMINISTIC, the same results in every
// build.
//
// Contacts are the simulation's to read (docs/architecture.md, "Physics"): a game system ordered
// after "physics" reads the step's, and one before it reads the last step's, which a save holds,
// so a world loaded from it hands the same contacts to the same systems. Two bodies touch while
// Jolt has a contact between them, which begins a couple of centimetres before they meet, as
// Jolt's speculative contacts do; a body touches a sensor while it overlaps it. A body asleep on
// another, or on a static body, still touches it, though Jolt drops a sleeping body's contacts.
// A body removed, or made again (a change of Body, system or frame), ends every contact it had in
// the step that removes it, and one made again begins its own afresh.
class PhysicsWorld final : public Sim::WorldService
{
  public:
	static constexpr std::string_view kSystemName = "physics";
	static constexpr std::string_view kServiceName = "physics";

	// Makes a physics world and gives it to the simulation world, which owns it from then on:
	// registers Body, Velocity, OnRails, Thrusters and FlightControls as components, the
	// SetFlightControls and Pick commands, the service, and the "flight" and "physics" systems. The
	// game registers Sim::Transform and Sim::InSystem itself, as it always does. Throws
	// std::logic_error as SimWorld's registrations do, for one already made.
	static PhysicsWorld &Attach(Sim::SimWorld &world, const PhysicsSettings &settings = {});

	~PhysicsWorld() override;

	// A shape bodies can name. Built here, so a shape Jolt refuses throws std::invalid_argument
	// naming it, as does a name already defined. Definitions are the game's start-up, the same
	// every run: a save names shapes, and a world it is loaded into has to define them.
	void DefineShape(std::string name, const ShapeDefinition &shape);
	// Every ShapeDefinition the registry has loaded, under its definition name.
	void DefineShapes(const Sim::DefinitionRegistry &definitions);
	bool HasShape(std::string_view name) const;

	// A star system's bodies, for rails and gravity in the system of that name (Sim::SystemId). It
	// has to be resolved (Celestial::Resolve), as reading one does; throws std::invalid_argument for
	// a system already defined.
	void DefineStarSystem(Celestial::StarSystem system);
	bool HasStarSystem(std::uint64_t system) const;
	// The acceleration the defined system's gravity gives a body at a position at a time, in metres
	// a second squared: each body with a gravitational parameter pulling as a point mass, and as one
	// whose mass is all within the point's distance from its centre, inside its radius. None in a
	// system that is not defined.
	glm::dvec3 Gravity(std::uint64_t system, const glm::dvec3 &position, double simSeconds) const;

	std::size_t BodyCount() const;
	std::size_t BodyCount(std::uint64_t system) const;
	// The frames of a system that have bodies: each is a Jolt world (LocalFrame).
	std::size_t FrameCount(std::uint64_t system) const;
	// The star systems that have bodies, in identifier order.
	std::vector<std::uint64_t> Systems() const;

	// The contacts the last step began and ended, in order of pair, and for one pair an end before
	// a beginning (a body made again).
	const std::vector<ContactEvent> &Contacts() const;
	// What touches the entity's body now, in entity order: none for an entity with no body.
	std::vector<entt::entity> Touching(entt::entity entity) const;

	// The first body a ray from the origin meets within the distance, across every frame of the
	// system, as the last step left them. A ray from inside a convex body hits it at once, and one
	// from inside a mesh, which has no inside, passes out through it. Throws std::invalid_argument
	// for a direction of no length or a distance that is not positive and finite.
	std::optional<CastHit> CastRay(
	    std::uint64_t system, const glm::dvec3 &origin, const glm::dvec3 &direction, double maxDistance,
	    const CastOptions &options = {}) const;
	// The first body the shape meets swept from where it starts along the direction, within the
	// distance: whether a ship fits through a gap, or how far it can go before it meets the
	// station. A shape that starts overlapping a body meets it at once. Throws std::invalid_argument
	// as CastRay does, and for a shape that is not defined.
	std::optional<CastHit> CastShape(
	    std::uint64_t system, std::string_view shape, const Sim::Transform &start,
	    const glm::dvec3 &direction, double maxDistance, const CastOptions &options = {}) const;

	// Every system's bodies, each with what it was made from and was last placed at, and Jolt's own
	// state: positions, velocities, sleep, and the contacts each step starts from; and the contacts
	// as the engine sees them, what touches and what the last step began and ended.
	void Save(Serialise::BinaryWriter &writer) const override;
	std::function<void()> Load(Serialise::BinaryReader &reader, const entt::registry &loaded) override;

  private:
	struct Impl;

	explicit PhysicsWorld(const PhysicsSettings &settings);
	// The step from simSeconds to simSeconds + fixedStep.
	void Step(entt::registry &registry, double simSeconds, double fixedStep);

	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Physics
