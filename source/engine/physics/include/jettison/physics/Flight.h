#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <string_view>

#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

namespace Jettison::Physics
{
// What a flown vehicle's thrust can do (its thrusters): accelerations rather than forces, so
// its handling does not change with the load it carries until a design wants it to. In its own
// frame, which is the Sim::Transform's: +X to starboard, +Y up, and forward along -Z, as a camera
// looks (the chase camera sits on +Z, behind). Registered with Reflect as "Physics.Handling", every
// field Saved, by RegisterTypes.
struct Handling
{
	// Metres a second squared: ahead (-Z), astern (+Z), to either side (X) and up or down (Y).
	float forward{30.0f};
	float reverse{15.0f};
	float lateral{15.0f};
	float vertical{15.0f};
	// Radians a second squared about the vehicle's X (pitch), Y (yaw) and Z (roll).
	glm::vec3 angularAcceleration{2.5f, 2.0f, 3.5f};
	// With flight assist, the speed the vehicle is held to relative to its reference, along each of its
	// axes, and how fast it is let turn about each.
	float maxSpeed{150.0f};
	glm::vec3 maxAngularRate{1.2f, 0.9f, 1.8f};

	bool operator==(const Handling &) const = default;
};

// What the pilot asks of the vehicle: simulation state, set by SetFlightControls and kept until the
// next one, so a recording holds a command when a control moves and not one a frame. Each demand is
// from -1 to 1, along or about the vehicle's axes as Handling has them (full ahead is linear.z = -1;
// pitching the nose up is angular.x = 1, a turn about +X). Registered with Reflect, every field
// Saved, by RegisterTypes.
//
// With assist on, a linear demand is a velocity relative to the reference, that share of
// maxSpeed, and an angular demand a rate of turn, that share of maxAngularRate: the thrusters
// accelerate towards them as fast as they can and hold them, so a vehicle let go stops, relative to
// the platform it flies by, and stops turning. With it off, a demand is that share of the
// thrusters' acceleration, and nothing is held: the vehicle coasts and spins on.
//
// The reference is the entity the vehicle's velocity is taken against, in the vehicle's frame
// (LocalFrame): the platform it flies by, which in its own frame is still, or another vehicle. A
// vehicle with no reference, or one that has gone or has no Velocity, is held still in its frame. A
// reference in another frame has a velocity that is not comparable, and is the game's mistake.
struct FlightControls
{
	glm::vec3 linear{0.0f};
	glm::vec3 angular{0.0f};
	bool assist{true};
	entt::entity reference{entt::null};

	bool operator==(const FlightControls &) const = default;
};

// The command a pilot's input becomes (docs/architecture.md, "Simulation and presentation"): the
// vehicle's FlightControls from the next step. A vehicle that is no longer in the world is not one to
// fly, and the command does nothing. Refused, with a FlightControlsRefused, from a sender not at
// the vehicle's helm (Sim::MayFly): a player flies only the vehicle they have the helm of, and the one
// whose behaviour flies it, or another player's, not at all. Registered with Reflect, every field
// Saved, by RegisterTypes.
struct SetFlightControls
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
	FlightControls controls;
};

// Why physics refuses a command (Sim::Reason), each worded by the string tables under
// reason.physics.<name>. Registered with Reflect as "Physics.Reason" by RegisterTypes.
enum class Reason : std::uint8_t
{
	NotAtHelm
};

// Raised as flight controls are refused, saying why. Registered with Reflect, every field Saved, by
// RegisterTypes.
struct FlightControlsRefused
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
	Sim::Reason reason;
};

// What a vehicle's thrusters fired over the last step, for presentation to show (its plumes and the
// glow of its nozzles): along and about each of the vehicle's axes, the share of that way's thrust it
// took, from -1 to 1, signed as FlightControls' demands are (full ahead is linear.z = -1, the nose
// pitching up angular.x = 1). It is what changed the vehicle's velocity, not what was asked: a vehicle
// held at a constant velocity, in space, which has nothing to slow it, fires nothing whatever its
// throttle. Written by the flight model for every vehicle it flies, and taken from one it no longer
// does (clamped to a berth, in transit, in a portal).
//
// Not state: nothing in the simulation reads it, and the world neither registers, saves nor hashes
// it, since each step makes it again from what is; a world loaded shows it from its first step on.
// Shown in snapshots.
struct Thrust
{
	glm::vec3 linear{0.0f};
	glm::vec3 angular{0.0f};

	bool operator==(const Thrust &) const = default;
};

inline constexpr std::string_view kFlightSystemName = "flight";

// The flight model's step, which PhysicsWorld::Attach registers as the system "flight", before
// "physics": each entity with Handling, FlightControls, a Sim::Transform and a Velocity (a dynamic
// body) has its velocity changed by what its thrusters do over the step, and physics then steps it
// with that velocity, so a vehicle flown into a platform meets it as any body would. Every vehicle's
// change is worked out before any is applied, so a vehicle whose reference is another vehicle sees that
// vehicle's velocity as the step began. Each vehicle flown has its Thrust written, and one with a
// Thrust that was not flown has it taken away.
void StepFlight(entt::registry &registry, double fixedStep);

// Whether the flight model's steps from the next on would leave every vehicle's Velocity as it is,
// which is when a step may pass it over (Sim::Quiet): none flown against a reference with a velocity
// or on rails, either of which may change what it is held to step to step, and none whose next change
// is not nothing; a kinematic body's in a
// frame it changes only for physics to write over. Thrust is not state, so it is left as the last step
// wrote it.
bool FlightAtRest(const entt::registry &registry, double fixedStep);
} // namespace Jettison::Physics
