#pragma once

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/dock/Definitions.h>
#include <jettison/sim/Controller.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::Dock
{
// Stations' pads and the ships on them, in the simulation (docs/architecture.md, "Docks"). Who
// holds a pad is authoritative state every player sees the same, so pads are entities of their
// stations, saved, and given by commands in the order the queue applies them. What a pad costs, who
// may land where and what loitering on one means are the game's; this is what any space game's
// stations would want unchanged.

// A ship's hull, by definition name (HullDefinition): what pad it fits. Its Physics::Body names the
// hull's shape (Docks::HullShape) for it to collide as its size. Registered with Reflect, its field
// Saved, by RegisterTypes.
struct Hull
{
	std::string definition;

	bool operator==(const Hull &) const = default;
};

// A station built to a layout: its pads, in their numbers' order, and the ships waiting for one,
// first come first. Registered with Reflect, every field Saved, by RegisterTypes.
struct Station
{
	std::string layout;
	std::vector<entt::entity> pads;
	std::vector<entt::entity> queue;

	bool operator==(const Station &) const = default;
};

enum class PadState : std::uint8_t
{
	Free,
	// Given to a ship, until it lands or the reservation lapses.
	Reserved,
	// A ship is clamped to it.
	Holding
};

// A pad, an entity of its station's, on the station's rails at its place on the deck
// (Physics::OnRails) and in its frame, so a ship's destination by it turns and moves with the
// station. The ship it is reserved for or holds, and the step its reservation lapses at
// (Time::SimClock::Step). Registered with Reflect, every field Saved, by RegisterTypes.
struct Pad
{
	entt::entity station{entt::null};
	std::uint32_t number{0};
	PadClass padClass{PadClass::S};
	bool display{false};
	PadState state{PadState::Free};
	entt::entity ship{entt::null};
	std::uint64_t lapses{0};

	bool operator==(const Pad &) const = default;
};

enum class Stage : std::uint8_t
{
	// Waiting in the station's queue for a pad.
	Queued,
	// Given a pad and on its way to the point above it, round the station's sphere.
	Inbound,
	// Coming down onto the pad from above it.
	Descending,
	// Clamped to the pad: held in the station's frame, on its rails, out of flight.
	Docked,
	// Let go and lifting to the point above the pad, which it gives up once there.
	Leaving
};

// A ship's business with a station: its place in the queue, or its pad and how far it has got. A
// ship flown by the docks (fly) is steered through each stage; one that is not is the game's or the
// pilot's to fly, and is clamped once it is on its pad as one flown would be. Registered with
// Reflect, every field Saved, by RegisterTypes.
struct Docking
{
	entt::entity station{entt::null};
	entt::entity pad{entt::null};
	Stage stage{Stage::Queued};
	bool fly{true};

	bool operator==(const Docking &) const = default;
};

// Whether the ship is clamped to a pad of the station.
bool IsDocked(const entt::registry &registry, entt::entity ship, entt::entity station);

// The command that asks a station for a pad for a ship, applied in the order commands are: the
// smallest free pad its hull fits, the lowest numbered of those, or, with none, a place at the back
// of the station's queue. Flown in by the docks if fly is set. Refused, with a PadRefused, for a
// sender that does not control the ship (Sim::MayCommand); a ship that is not a dynamic body with a
// hull, or is in another system or frame than the station; one that already holds a pad or a place;
// and a station that is not one, or has no pad the hull fits. Registered with Reflect, every field
// Saved, by RegisterTypes.
struct RequestPad
{
	Sim::Controller sender;
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	bool fly{true};
};

// The command that gives up a ship's pad: its place in the queue, or its reservation, which frees
// the pad for the next ship; or, docked, it is let go and lifted clear (Stage::Leaving), and gives
// the pad up once there. Refused for a sender that does not control the ship, and for a ship with
// nothing to give up. Registered with Reflect, every field Saved, by RegisterTypes.
struct ReleasePad
{
	Sim::Controller sender;
	entt::entity ship{entt::null};
};

// The answers, raised as events (Sim::EventQueue): a pad given, a place in the queue (from 1), a
// refusal and why, a reservation lapsed, a ship clamped to its pad and let go of it, and a pad or a
// place given up, or left behind once a ship has lifted clear. Registered with Reflect, every
// field Saved, by RegisterTypes.
struct PadGiven
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	entt::entity pad{entt::null};
	std::uint32_t number{0};
};
struct PadQueued
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	std::uint32_t place{0};
};
struct PadRefused
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	std::string reason;
};
struct PadLapsed
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	entt::entity pad{entt::null};
	std::uint32_t number{0};
};
struct PadReleased
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
};
struct Landed
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	entt::entity pad{entt::null};
	std::uint32_t number{0};
};
struct Left
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
	entt::entity pad{entt::null};
	std::uint32_t number{0};
};

// The docks in a simulation world: the components, the commands, and the system "dock"
// (kSystemName), after physics' ("physics"), which each step, in entity order:
//   1. Gives up what a ship can no longer hold: a pad or a place whose ship has gone, or has left
//      its station's system or frame, and a ship's business with a station that has gone.
//   2. Lapses each reservation whose step has come, freeing the pad.
//   3. Moves each ship on: one on its way to its pad, within a clamp's reach of it (2 m across it,
//      a metre above or below the hull's resting height), turned to it (its top within 10° of the
//      normal, its nose within 15° of the heading), and moving at under half a metre a second, is
//      clamped there, docked; one flown in that has reached the point above its pad comes down onto
//      it; and one leaving that has reached the point above its pad gives it up.
//   4. Serves each station's queue from the front, each ship the smallest free pad it fits.
//
// A ship flown in by the docks is steered (Ai::Steering): first along a course round the station's
// sphere (Ai::Waypoints) to the point above its pad, outside that sphere along the pad's normal;
// then down the normal onto the pad, kept clear of the station by its hull (Ai::Destination::byHull),
// turned to the pad, slowing to stop a hand's breadth above it; and once clamped its
// Physics::Body is kinematic, on the station's rails at its place on the pad. Undocking makes it
// dynamic again and lifts it the same way back up.
//
// The definitions are the game's start-up, as physics' shapes are, and not state: a save names
// layouts and hulls, and a world it is loaded into is given the same ones.
class Docks
{
  public:
	static constexpr std::string_view kSystemName = "dock";

	// Registers the components, the commands and the system, ordered after physics', so physics
	// has to be attached first; and steering, whose destinations the docks set. Throws
	// std::logic_error as SimWorld's registrations do, for one already made.
	static Docks &Attach(Sim::SimWorld &world, Physics::PhysicsWorld &physics);

	// The physics shapes a hull's and a layout's bodies name: "hulls/<name>", "stations/<name>".
	static std::string HullShape(std::string_view hull);
	static std::string StationShape(std::string_view layout);

	// A hull and a layout, checked (CheckHull, CheckLayout), and their shapes defined in physics.
	// Throws Serialise::FormatError for one that does not fit, and std::invalid_argument for a name
	// already defined.
	void DefineHull(std::string name, HullDefinition hull);
	void DefineLayout(std::string name, StationLayout layout);
	// Every HullDefinition and StationLayout the registry has loaded.
	void Define(const Sim::DefinitionRegistry &definitions);
	const HullDefinition *FindHull(std::string_view name) const;
	const StationLayout *FindLayout(std::string_view name) const;

	// A station's entities in the world, for the celestial body of that name in the system, built
	// to the layout: its kinematic body on the body's rails, in the body's frame, colliding as its
	// layout's decks and piers, and an entity for each pad, on the same rails at its place. All of
	// it placed where the rails have it now. The system has to be defined in physics; throws
	// std::invalid_argument for a layout not defined.
	entt::entity MakeStation(
	    Sim::SimWorld &world, std::uint64_t system, std::string_view body, std::string_view layout);

	// A ship put on a pad and clamped there, docked, as a game parks one at its start: a dynamic body
	// with a hull that fits the pad, in the station's system. Throws std::invalid_argument for a pad
	// that is not free, a display pad among them, or a ship that cannot be put on it.
	void Park(Sim::SimWorld &world, entt::entity ship, entt::entity pad);

  private:
	explicit Docks(Physics::PhysicsWorld &physics) : m_physics{physics}
	{
	}

	void Request(Sim::SimWorld &world, const RequestPad &command);
	void Release(Sim::SimWorld &world, const ReleasePad &command);
	void Step(Sim::SimWorld &world);

	// The smallest free pad the ship's hull fits, the lowest numbered of those, or none.
	entt::entity FreePadFor(
	    const entt::registry &registry, const Station &station, entt::entity ship) const;
	// Gives each ship in the station's queue, from the front, the smallest free pad it fits. The
	// time is the one the world's transforms are at: the step's start in a command, its end in the
	// system.
	void Serve(Sim::SimWorld &world, entt::entity station, double time);
	void Give(Sim::SimWorld &world, entt::entity ship, entt::entity pad, double time);
	// Frees the ship's pad or its place, lets it go if it is clamped, takes away its business with
	// the station and, if the docks flew it, its destination; and says so if asked to.
	void GiveUp(Sim::SimWorld &world, entt::entity ship, bool announce);
	// The destinations of each stage the docks fly a ship through.
	void FlyInbound(Sim::SimWorld &world, entt::entity ship, double time);
	void FlyDown(Sim::SimWorld &world, entt::entity ship);
	void FlyUp(Sim::SimWorld &world, entt::entity ship);
	bool InClampReach(const entt::registry &registry, entt::entity ship) const;
	void Clamp(Sim::SimWorld &world, entt::entity ship, double time);
	void Unclamp(Sim::SimWorld &world, entt::entity ship);

	const PadLayout &PadOf(const entt::registry &registry, const Pad &pad) const;
	const HullDefinition *HullOf(const entt::registry &registry, entt::entity ship) const;
	// How high above the pad, along its normal, a ship clears the station's sphere, its own hull and
	// its clearance beyond that: the point it comes down from and lifts to.
	double AboveHeight(const entt::registry &registry, entt::entity ship, const Pad &pad) const;
	// Where a ship of the hull rests on the pad: its centre half its height above the deck, in the
	// pad's axes.
	static double RestingHeight(const HullDefinition &hull);

	Physics::PhysicsWorld &m_physics;
	std::map<std::string, HullDefinition, std::less<>> m_hulls;
	std::map<std::string, StationLayout, std::less<>> m_layouts;
};
} // namespace Jettison::Dock
