#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/celestial/StarSystem.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Sim
{
class SimWorld;
}

namespace Jettison::Ai
{
class LaneTravel;

// A gate flown through (docs/roadmap.md, "Decided": a gate sends the ships it has cleared). A jump
// point's gate is an entity with a ring, a sensor on the jump point's rails, and a ship it has cleared
// that flies through the ring is sent along its lane (Lanes.h), its owner charged the lane's toll as
// it enters. Who holds a gate is authoritative state every player sees the same, so the gate is
// given by commands in the order the queue applies them, as a station's pad is (dock/Docks.h). What
// a toll is drawn from, and whatever else a clearance asks of a ship's owner, are the game's
// (TollRule); this is what any space game's gates would want unchanged.

// A jump point's gate: its ring, the ship it has cleared and the step that clearance lapses at
// (Time::SimClock::Step), and the ships waiting for it, first come first. Free while it has cleared
// none. Its entity is on the jump point's rails, in its frame, a kinematic sensor of the ring's
// shape (Gates::RingShape) turned to face as the jump point does. Registered with Reflect, every
// field Saved, by RegisterTypes, and shown in snapshots.
struct Gate
{
	std::string jumpPoint;
	entt::entity cleared{entt::null};
	std::uint64_t lapses{0};
	std::vector<entt::entity> queue;

	bool operator==(const Gate &) const = default;
};

// A ship's business with a gate: waiting in its queue, or cleared through it. Registered with
// Reflect, every field Saved, by RegisterTypes, and shown in snapshots.
struct Transit
{
	entt::entity gate{entt::null};
	bool cleared{false};

	bool operator==(const Transit &) const = default;
};

// The command that asks a gate to clear a ship through it, applied in the order commands are:
// cleared at once if the gate is free, or else given a place at the back of its queue, cleared as
// the ships before it have gone through or let their clearances lapse. Refused, with a
// TransitRefused, for a sender that does not command the ship (Sim::MayCommand); a ship on a lane,
// in no system, cruising, or not a dynamic body off rails; a gate that is not one, or one in another
// system or frame than the ship; a ship already cleared or waiting at a gate; a gate no lane leaves
// through, or whose lane arrives in a system not defined; a hull too large for the ring (its shape's
// radius against the jump point's); and whatever the game's rule refuses (TollRule). Registered with
// Reflect, every field Saved, by RegisterTypes.
struct RequestTransit
{
	Sim::Controller sender;
	entt::entity ship{entt::null};
	entt::entity gate{entt::null};
};

// The answers, raised as events (Sim::EventQueue): a ship cleared, with the toll it will be charged
// and the step its clearance lapses at; a place in the queue (from 1); a refusal and why; a clearance
// or a place given up unused, as it lapses or as its ship goes, leaves the gate's system or frame, or
// cruises off; a ship sent through, and the toll its owner was charged; and a ship that flew through
// the ring and was not sent, and why, which is charged nothing and flies on. Registered with Reflect,
// every field Saved, by RegisterTypes.
struct TransitCleared
{
	entt::entity ship{entt::null};
	entt::entity gate{entt::null};
	std::int64_t toll{0};
	std::uint64_t lapses{0};
};
struct TransitQueued
{
	entt::entity ship{entt::null};
	entt::entity gate{entt::null};
	std::uint32_t place{0};
};
struct TransitRefused
{
	entt::entity ship{entt::null};
	entt::entity gate{entt::null};
	Sim::Reason reason;
};
struct TransitLapsed
{
	entt::entity ship{entt::null};
	entt::entity gate{entt::null};
};
struct TransitSent
{
	entt::entity ship{entt::null};
	entt::entity gate{entt::null};
	std::int64_t toll{0};
};
struct GatePassed
{
	entt::entity ship{entt::null};
	entt::entity gate{entt::null};
	Sim::Reason reason;
};

// The game's say in a transit: whether a ship's owner can pay a lane's toll, and the charge. Asked
// to clear a ship, as a RequestTransit finds the gate free or the gate frees with the ship first in
// its queue, refuses gives a reason not to, or nothing; asked again as a cleared ship touches the
// ring, since its owner may have spent the credits since, when a reason lets the ship pass through
// unsent, its clearance given up. charge takes the toll once the ship is on the lane. Either may be
// empty: without a rule, every lane is free.
struct TollRule
{
	std::function<std::optional<Sim::Reason>(
	    const entt::registry &registry, entt::entity ship, std::int64_t toll)>
	    refuses;
	std::function<void(entt::registry &registry, entt::entity ship, std::int64_t toll)> charge;
};

// The gates in a simulation world: the components, the command, and the system "gates"
// (kSystemName), after the lanes' ("lanes"), which each step, in entity order:
//   1. Gives up what a ship can no longer hold: a clearance or a place whose ship has gone, is on a
//      lane or cruising, or has left the gate's system or frame, and one at a gate that has gone.
//   2. Sends each gate's cleared ship that touches its ring (Physics::PhysicsWorld::Touching), so a
//      ship cleared while already inside it goes too: along the lane (EnterLane, applied then), its
//      owner charged the toll (TollRule) and the gate freed. A body that began to touch a ring in the
//      step and was not sent passes through, saying why (GatePassed), and is charged nothing.
//   3. Lapses each clearance whose step has come, freeing the gate.
//   4. Serves each free gate's queue from the front.
// A ring is placed, not swept, so it is kRingDepth deep along its axis: a ship crossing it at under
// that many metres a step, 3.6 km/s at 60 steps a second, is seen.
//
// The star systems are definitions, the game's start-up as physics' shapes are, and not state: a
// save names a gate's jump point, and a world it is loaded into is given the same systems.
//
// The world holds it from Attach on, through the systems and commands it registers.
class Gates
{
  public:
	static constexpr std::string_view kSystemName = "gates";
	// Metres along a ring's axis that its sensor spans, either side of the jump point.
	static constexpr double kRingDepth = 60.0;
	// Seconds a clearance is held for its ship, which has to fly through the ring by then: a cruise
	// arrives 4.6 km off a gate, which the sample's traders fly through within 34 s of being cleared,
	// and a pilot flying by hand has more than three times that.
	static constexpr double kClearanceSeconds = 120.0;

	// Registers the components, the command and the "gates" system, ordered after the lanes'
	// (LaneTravel::Attach), which have to be attached first, and given the physics world the rings
	// are shapes and bodies of. Throws std::logic_error as SimWorld's registrations do, for one
	// already made.
	static Gates &Attach(Sim::SimWorld &world, Physics::PhysicsWorld &physics, const LaneTravel &lanes);

	// The physics shape of a ring of that radius: "rings/<radius>", a cylinder of the radius and
	// kRingDepth along its Y axis.
	static std::string RingShape(double radius);

	// A star system, for its jump points' rings, whose shapes are defined in physics, once each. It
	// has to be resolved; throws std::invalid_argument for a system already defined, and for a jump
	// point of no radius. A RequestTransit in a system not defined is refused.
	void DefineStarSystem(const Celestial::StarSystem &system);
	bool HasStarSystem(std::uint64_t system) const;

	// The game's rule for tolls, replacing any given before.
	void SetTollRule(TollRule rule);

	// A gate for each jump point of the defined system (Sim::SystemId), as a game makes them at its
	// start, placed where the rails have it now; a save holds them after. Throws
	// std::invalid_argument for a system not defined, and std::logic_error for one whose gates are
	// already made. Returns them, in the jump points' order.
	std::vector<entt::entity> MakeGates(Sim::SimWorld &world, std::uint64_t system) const;

	// The gate of that jump point in the system, or none.
	static entt::entity FindGate(
	    const entt::registry &registry, std::uint64_t system, std::string_view jumpPoint);

  private:
	Gates(Physics::PhysicsWorld &physics, const LaneTravel &lanes) : m_physics{physics}, m_lanes{lanes}
	{
	}

	void Request(Sim::SimWorld &world, const RequestTransit &command);
	void Step(Sim::SimWorld &world);

	// The gate's jump point in its system's definition, or null.
	const Celestial::Body *JumpPointOf(const entt::registry &registry, entt::entity gate) const;
	// The lane's toll through the gate, or none for a gate no lane leaves through.
	std::optional<std::int64_t> TollThrough(const entt::registry &registry, entt::entity gate) const;
	// Why the game's rule refuses the ship, or nothing.
	std::optional<Sim::Reason> Refuses(
	    const entt::registry &registry, entt::entity ship, std::int64_t toll) const;
	// Clears the ship through the free gate, and says so.
	void Clear(Sim::SimWorld &world, entt::entity gate, entt::entity ship, std::int64_t toll);
	// Takes the ship's business with its gate away: its clearance, freeing the gate, or its place.
	void GiveUp(entt::registry &registry, entt::entity ship);
	// Sends the cleared ship along the gate's lane and charges its owner, or lets it pass through,
	// saying why, its clearance given up.
	void Send(Sim::SimWorld &world, entt::entity gate, entt::entity ship);
	// Clears ships from the front of a free gate's queue until one is cleared or none waits; a ship
	// the rule now refuses is told why and leaves the queue.
	void Serve(Sim::SimWorld &world, entt::entity gate);

	Physics::PhysicsWorld &m_physics;
	const LaneTravel &m_lanes;
	TollRule m_rule;
	// By Sim::SystemId; looked up, and ordered regardless.
	std::map<std::uint64_t, Celestial::StarSystem> m_systems;
};
} // namespace Jettison::Ai
