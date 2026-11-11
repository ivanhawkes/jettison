#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace Jettison::Physics
{
// How a body moves. A static body stays where its Transform puts it and is moved only by writing
// the Transform again; a kinematic body follows its Transform, which its owner writes before
// physics runs (a station on its orbit), and pushes what it meets with the velocity that implies;
// a dynamic body is moved by physics, which writes its Transform and Velocity after every step.
enum class BodyKind : std::uint8_t
{
	Static,
	Kinematic,
	Dynamic
};

// A simulation entity in the physics world (PhysicsWorld.h): with a Sim::Transform and a
// Sim::InSystem naming a star system, it is a body in that system's physics, and one in no system
// (on a jump lane) is in none. Read when the body is made; a change to any field remakes the body
// at the next step, keeping its velocity. Registered with Reflect, every field Saved, by
// RegisterTypes.
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

	bool operator==(const Body &) const = default;
};

// A dynamic body's velocity in its system's frame: metres a second, and radians a second about
// each axis. Physics writes it after every step, and a value written over it before a step is the
// body's new velocity. Physics adds one, at rest, to a dynamic body that has none. Floats, as Jolt
// keeps it. Registered with Reflect, both fields Saved, by RegisterTypes.
struct Velocity
{
	glm::vec3 linear{0.0f};
	glm::vec3 angular{0.0f};

	bool operator==(const Velocity &) const = default;
};

// Registers the module's types with Reflect, and the simulation's (Sim::RegisterTypes).
// Idempotent; PhysicsWorld::Attach calls it.
void RegisterTypes();
} // namespace Jettison::Physics
