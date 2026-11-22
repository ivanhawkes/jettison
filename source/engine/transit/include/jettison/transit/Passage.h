#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/physics/Components.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/transit/Profile.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Sim
{
class SimWorld;
}

namespace Jettison::Transit
{
// A passage out of physics, any game's (docs/architecture.md, "Transit"): a lift's car up its shaft,
// a train along its line, a vehicle carried across a frame faster than it could fly. The entity
// leaves physics with its body kept, is placed each step along a way the game gives (Path.h) about
// what carries it, and is brought out at rest, clear of what is there, in a local frame the game
// names. Passages runs passages kept in a Passage; a game whose passage saves fields of its own
// keeps its own component and builds on the parts below.

// A steered entity's Ai::Destination given up as it leaves its frame for a passage, its controls let
// go with it, as Ai::ClearDestination lets them go: what steering last demanded would otherwise fly
// it on from where it comes out. One with no Destination keeps its pilot's controls.
void GiveUpDestination(entt::registry &registry, entt::entity entity);

// Takes an entity with a Physics::Body out of physics as a passage sets out: its Body, Velocity and
// local frame (Physics::LocalFrame) removed and its Destination given up. Returns the Body, which it
// has back on arrival (BringOut).
Physics::Body TakeOut(entt::registry &registry, entt::entity entity);

// Brings an entity out of a passage: at the place, with its body back, which physics makes in the
// next step, at rest in the local frame, and its Physics::FlightControls taken against nothing.
void BringOut(
    entt::registry &registry, entt::entity entity, const Sim::Transform &place,
    const Physics::Body &body, std::string frame);

// What an arrival comes out clear of: each body in a frame (Sim::InFrame), where it is as the step
// ends, and the sphere that holds its shape.
struct Occupant
{
	std::uint64_t frame{0};
	glm::dvec3 position{0.0};
	double radius{0.0};
};

// Every entity with a Physics::Body in one of the frames, but a sensor, which nothing meets.
std::vector<Occupant> Occupants(
    const entt::registry &registry, const Physics::PhysicsWorld &physics,
    std::span<const std::uint64_t> frames);

// Where an arrival of that radius comes out in the frame: at the place if it is clear of every
// occupant there by both radii and the clearance, or else at the first place clear on rings about
// it, in the plane across the way it faces (its x and y axes), each ring the arrival's breadth and
// the clearance further out, with six more places round each than the last.
glm::dvec3 ClearPlace(
    std::span<const Occupant> occupants, std::uint64_t frame, const Sim::Transform &place,
    double radius, double clearance);

// An entity on a passage: its Sim::InFrame stays the frame it is in, and it has no Physics::Body,
// Velocity or LocalFrame from the step it set out in to the step it arrives in; its Transform is
// where its way puts it at each step's end. Registered with Reflect, every field Saved, by
// RegisterTypes, and shown in snapshots.
struct Passage
{
	// The local frame it arrives in (Physics::LocalFrame), or none for the frame's own.
	std::string frame;
	// What its way moves with, which the game's Locate places, or none for the frame's origin.
	std::string carrier;
	// Its way (Path.h), at least one point.
	std::vector<glm::dvec3> path;
	// The clock's step counts as it set out and once it has arrived (Time::SimClock::Step): it
	// arrives as the step before the second ends.
	std::uint64_t departureStep{0};
	std::uint64_t arrivalStep{0};
	// Its body, which it has back on arrival.
	Physics::Body body;
	// The profile it covers its way by, or none to ease along it.
	std::optional<MotionProfile> profile;

	bool operator==(const Passage &) const = default;
};

// Raised as an entity arrives. Registered with Reflect, every field Saved, by RegisterTypes.
struct PassageEnded
{
	entt::entity entity{entt::null};
};

// Passages in a simulation world: the Passage component and the system "transit" (kSystemName),
// which runs after physics' and places each entity on a passage as the step ends:
//   - before the step it arrives in, where its way has it (Underway);
//   - in that step, at its way's end, or if that is not clear of every body already there and of
//     what arrived before it in the step, at the first place clear (ClearPlace, kArrivalClearance);
//     facing along its way's last leg, brought out (BringOut), and a PassageEnded raised.
// Entities are placed in entity order, and a step that only places them may be passed over
// (Sim::Quiet).
//
// The world holds it from Attach on, through the system it registers.
class Passages
{
  public:
	static constexpr std::string_view kSystemName = "transit";
	// Metres an arrival keeps from what is already there.
	static constexpr double kArrivalClearance = 1.0;

	// Where a carrier is at a time, in the frame (Sim::InFrame). The game's, a definition and not
	// state, the same every run; asked only of a passage with a carrier.
	using Locate =
	    std::function<glm::dvec3(std::uint64_t frame, std::string_view carrier, double time)>;

	// Registers the component and the "transit" system, ordered after physics, given the physics
	// world whose shapes say how far apart arrivals come out, and the game's carriers. Throws
	// std::logic_error as SimWorld's registrations do, for one already made.
	static Passages &Attach(
	    Sim::SimWorld &world, const Physics::PhysicsWorld &physics, Locate locate = {});

	// Sends an entity with a Physics::Body, a Sim::Transform and a Sim::InFrame on a passage taking
	// that many seconds (Steps), from this step: its Body taken out (TakeOut), its departure and
	// arrival steps set. Throws std::invalid_argument for an entity without them, one already on a
	// passage, and a way with no point.
	static void Begin(Sim::SimWorld &world, entt::entity entity, Passage passage, double seconds);

  private:
	Passages(const Physics::PhysicsWorld &physics, Locate locate)
	    : m_physics{physics}, m_locate{std::move(locate)}
	{
	}

	glm::dvec3 Origin(std::uint64_t frame, const Passage &passage, double time) const;

	void Place(Sim::SimWorld &world) const;

	// Places each entity on a passage as the last step passed over would have, none arriving.
	void Skip(Sim::SimWorld &world) const;

	const Physics::PhysicsWorld &m_physics;
	Locate m_locate;
};
} // namespace Jettison::Transit
