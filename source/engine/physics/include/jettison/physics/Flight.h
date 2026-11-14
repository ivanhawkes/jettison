#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <string_view>

namespace Jettison::Physics
{
// What a ship's thrusters can do: accelerations rather than forces, so a ship's handling does not
// change with the cargo it carries until a design wants it to. In the ship's own frame, which is
// the Sim::Transform's: +X to starboard, +Y up, and forward along -Z, as a camera looks (the chase
// camera sits on +Z, behind). Registered with Reflect, every field Saved, by RegisterTypes.
struct Thrusters
{
	// Metres a second squared: ahead (-Z), astern (+Z), to either side (X) and up or down (Y).
	float forward{30.0f};
	float reverse{15.0f};
	float lateral{15.0f};
	float vertical{15.0f};
	// Radians a second squared about the ship's X (pitch), Y (yaw) and Z (roll).
	glm::vec3 angularAcceleration{2.5f, 2.0f, 3.5f};
	// With flight assist, the speed the ship is held to relative to its reference, along each of its
	// axes, and how fast it is let turn about each.
	float maxSpeed{150.0f};
	glm::vec3 maxAngularRate{1.2f, 0.9f, 1.8f};

	bool operator==(const Thrusters &) const = default;
};

// What the pilot asks of the ship: simulation state, set by SetFlightControls and kept until the
// next one, so a recording holds a command when a control moves and not one a frame. Each demand is
// from -1 to 1, along or about the ship's axes as Thrusters has them (full ahead is linear.z = -1;
// pitching the nose up is angular.x = 1, a turn about +X). Registered with Reflect, every field
// Saved, by RegisterTypes.
//
// With assist on, a linear demand is a velocity relative to the reference, that share of
// maxSpeed, and an angular demand a rate of turn, that share of maxAngularRate: the thrusters
// accelerate towards them as fast as they can and hold them, so a ship let go stops, relative to
// the station it flies by, and stops turning. With it off, a demand is that share of the
// thrusters' acceleration, and nothing is held: the ship coasts and spins on.
//
// The reference is the entity the ship's velocity is taken against, in the ship's frame
// (LocalFrame): the station it flies by, which in its own frame is still, or another ship. A ship
// with no reference, or one that has gone or has no Velocity, is held still in its frame. A
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
// ship's FlightControls from the next step. A ship that is no longer in the world is not one to
// fly, and the command does nothing. Registered with Reflect, every field Saved, by RegisterTypes.
struct SetFlightControls
{
	entt::entity ship{entt::null};
	FlightControls controls;
};

// What a ship's thrusters fired over the last step, for presentation to show (its plumes and the
// glow of its nozzles): along and about each of the ship's axes, the share of that way's thrust it
// took, from -1 to 1, signed as FlightControls' demands are (full ahead is linear.z = -1, the nose
// pitching up angular.x = 1). It is what changed the ship's velocity, not what was asked: a ship
// held at a constant velocity, in space, which has nothing to slow it, fires nothing whatever its
// throttle. Written by the flight model for every ship it flies, and taken from one it no longer
// does (clamped to a pad, cruising, on a lane).
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
// "physics": each entity with Thrusters, FlightControls, a Sim::Transform and a Velocity (a dynamic
// body) has its velocity changed by what its thrusters do over the step, and physics then steps it
// with that velocity, so a ship flown into a station meets it as any body would. Every ship's change
// is worked out before any is applied, so a ship whose reference is another ship sees that ship's
// velocity as the step began. Each ship flown has its Thrust written, and one with a Thrust that was
// not flown has it taken away.
void StepFlight(entt::registry &registry, double fixedStep);
} // namespace Jettison::Physics
