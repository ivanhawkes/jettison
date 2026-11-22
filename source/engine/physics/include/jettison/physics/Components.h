#pragma once

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

#include <jettison/serialise/Migration.h>

namespace Jettison::Physics
{
// How a body moves. A static body stays where its Transform puts it and is moved only by writing
// the Transform again; a kinematic body follows its Transform, which its owner writes before
// physics runs (a body on its orbit), and pushes what it meets with the velocity that implies;
// a dynamic body is moved by physics, which writes its Transform and Velocity after every step.
enum class BodyKind : std::uint8_t
{
	Static,
	Kinematic,
	Dynamic
};

// A simulation entity in the physics world (PhysicsWorld.h): with a Sim::Transform and a
// Sim::InFrame, it is a body in that frame's physics, and one in no frame (a vehicle in
// transit between frames) is in none. Read when the body is made; a change to any field remakes the
// body at the next step, keeping its velocity. Registered with Reflect, every field Saved, by
// RegisterTypes; PhysicsWorld::Attach registers it with its chain (Migrations).
//
// Jolt keeps a body's properties in single precision, so these are floats: a double here would
// claim precision the body does not have.
struct Body
{
	BodyKind kind{BodyKind::Dynamic};
	// A shape defined in the physics world, by name (PhysicsWorld::DefineShape).
	std::string shape;
	// In kilograms, for a dynamic body, whose inertia is the shape's scaled to it. Static and
	// kinematic bodies do not use it.
	float mass{0.0f};
	float friction{0.2f};
	float restitution{0.0f};
	// The fraction of velocity lost a second. Space has no drag, so both are zero unless a design
	// wants them.
	float linearDamping{0.0f};
	float angularDamping{0.0f};
	// A dynamic body swept along its motion each step rather than placed at its end, so it cannot
	// pass through what it meets: a piloted vehicle, which approaches a platform at tens of metres a
	// second and would cross a thin panel between two steps. Jolt sweeps it against each body by
	// their relative motion, so a vehicle and a platform both moving at a planet's orbital speed are
	// swept by the little that separates them. It costs a cast a step, so a load goes without.
	bool continuous{false};
	// A body that meets nothing and reports what enters it (PhysicsWorld::Contacts): a doorway, or
	// a zone a game's rule watches. It detects dynamic bodies, never static or kinematic ones, and
	// pushes none. A sensor never sleeps, so a vehicle come to rest inside it is still reported inside;
	// it is placed rather than swept, so it is not continuous, and a body fast enough to cross it
	// between two steps is not seen.
	bool sensor{false};

	bool operator==(const Body &) const = default;

	// Version 2 (the space extraction) renamed the shapes the docks name, from "hulls/" and
	// "stations/" to "vehicles/" and "platforms/" (Dock::Docks::VehicleShape, PlatformShape).
	static const Serialise::Migrations<Serialise::BinaryRecord> &Migrations();
};

// A moving body's velocity relative to its frame (LocalFrame): metres a second, and radians a second
// about each axis. Physics writes it after every step, for a dynamic body and for a kinematic one,
// whose velocity is what moving it to its Transform took; a value written over a dynamic body's
// before a step is the body's new velocity, and one written over a kinematic body's is replaced.
// Physics adds one, at rest, to a moving body that has none. Floats, as Jolt keeps it, which is
// why it is relative to the frame: a vehicle beside a platform is a few metres a second from still,
// where in the enclosing frame it moves at the planet's 30 km/s, and a float's step there is
// 4 mm/s.
// Velocities in different frames are not comparable. Registered with Reflect, both fields Saved,
// by RegisterTypes.
struct Velocity
{
	glm::vec3 linear{0.0f};
	glm::vec3 angular{0.0f};

	bool operator==(const Velocity &) const = default;
};

// An entity on rails: each step its Transform is put where the body of that name in the frame its
// Sim::InFrame names is at the time the step ends, and turned as that body is (FrameModel::BodyAt):
// in Manifest, a planet, spinning, or a portal, facing as its ring does. A platform's body is kinematic
// and on rails, so it pushes a vehicle it meets as its orbit carries it, and is where presentation
// draws it. The frame has to be defined in the physics world (PhysicsWorld::DefineFrame); an entity in
// no frame is left where it is. Registered with Reflect, every field Saved, by RegisterTypes.
//
// The offset puts it at a place on the body rather than at its centre, in the body's own axes, and
// the turn turns it there, so it turns with the body: a berth on a platform's deck, and a vehicle
// clamped to the berth (Dock), which is held where the berth is at every step.
struct OnRails
{
	std::string body;
	glm::dvec3 offset{0.0};
	glm::dquat turn{1.0, 0.0, 0.0, 0.0};

	bool operator==(const OnRails &) const = default;
};

// The frame a body's physics runs in (docs/architecture.md, "Coordinate frames and units"): the
// local frame of that body of its frame's (FrameModel), in the space sample a platform or a planet,
// which moves with the body and does not turn with it. A body without one is in the frame's own,
// at rest at its origin. Each local frame with bodies is a Jolt world of its own, so only bodies in
// one meet, and a body that changes local frame is made again in the other's, its velocity carried
// across. Jolt sweeps a fast body and sorts the broadphase in floats: relative to a platform they are
// metres and metres a second, where relative to a star a platform 150 million kilometres out is
// 8 km between floats and moves 500 m a step, and nothing sweeps against it. The frame has to be
// defined (PhysicsWorld::DefineFrame), and a static body is in the frame's own: one carried by a
// local frame is a kinematic body. Registered with Reflect, its field Saved, by RegisterTypes.
struct LocalFrame
{
	std::string body;

	bool operator==(const LocalFrame &) const = default;
};

// Registers the module's types with Reflect, and the simulation's (Sim::RegisterTypes).
// Idempotent; PhysicsWorld::Attach calls it.
void RegisterTypes();
} // namespace Jettison::Physics
