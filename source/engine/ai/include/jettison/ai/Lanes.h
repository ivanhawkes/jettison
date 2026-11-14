#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include <jettison/ai/Types.h>
#include <jettison/celestial/Galaxy.h>
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
// The simulation's jump (docs/architecture.md, "Jumps"): an entity's passage along a jump lane, out
// of its system through a jump point, in none for the lane's transit time, and into the system at
// the lane's far end, at the jump point there. What sends a ship along a lane, the pilot flying
// through the gate's ring that cleared it (Gates.h), or a tool, is the game's; this is how the
// simulation carries it.

// An entity on a lane: its Sim::InSystem names no system (Sim::kNoSystem) from the step it entered
// to the step it arrives in. Its Transform is where it left, which nothing shows, since an entity
// in no system is in no system scene. Registered with Reflect, every field Saved, by RegisterTypes.
struct OnLane
{
	// The system it left (Sim::SystemId) and the jump point it entered by.
	std::uint64_t from{0};
	std::string departure;
	// The system it arrives in and the jump point it arrives at.
	std::uint64_t to{0};
	std::string arrival;
	// The clock's step count once it has arrived (Time::SimClock::Step): it arrives as the step
	// before that one ends.
	std::uint64_t arrivalStep{0};

	bool operator==(const OnLane &) const = default;
};

// The command that sends an entity along the lane leaving its system through that jump point,
// applied as the step begins, or as a gate applies it to a ship it has cleared. The entity gives up
// its Destination (Steering.h), which is in the system it leaves. Refused, with a LaneRefused, for a
// sender that does not command the entity (Sim::MayCommand), an entity not in a system of the galaxy
// (one already on a lane is in none), a cruising one, or a jump point no lane leaves through. A gate
// sends a ship it cleared as no one in particular, the game's rule; a player asks for a gate's
// clearance (RequestTransit). Registered with Reflect, every field Saved, by RegisterTypes.
struct EnterLane
{
	Sim::Controller sender;
	entt::entity entity{entt::null};
	std::string jumpPoint;
};

// Raised as an entity enters a lane, and as it arrives at the far end. Registered with Reflect,
// every field Saved, by RegisterTypes.
struct LaneEntered
{
	entt::entity entity{entt::null};
	OnLane lane;
	double transitSeconds{0.0};
};
struct LaneLeft
{
	entt::entity entity{entt::null};
	std::uint64_t system{0};
	std::string arrival;
};
struct LaneRefused
{
	entt::entity entity{entt::null};
	std::string jumpPoint;
	Sim::Reason reason;
};

// Lane travel in a simulation world: the OnLane component, the EnterLane command, and the system
// "lanes" (kSystemName), which runs after physics' and brings out each entity whose transit ends
// with the step:
//   - its Sim::InSystem names the system it arrives in, and its Transform is at the jump point
//     arrived at, where the jump point is as the step ends, turned the way the jump point faces
//     (Celestial::JumpPointFacing): towards its planet, with its station before it;
//   - an entity with a Physics::Body arrives at rest in the frame of the station the jump point
//     orbits (Physics::LocalFrame), or the jump point's own frame if it orbits none, as a body has
//     to be in a frame: the gate is hundreds of millions of kilometres from the primary, where
//     Jolt's floats are kilometres apart. In the station's frame a ship meets and docks with the
//     station without changing frames on the way. At rest, so the speed a ship went into the far
//     gate at is not carried out of this one at a station kilometres ahead of it; physics makes its
//     body in the next step;
//   - with a Physics::Body, it comes out ahead of the gate's ring, clear of its sensor (half of
//     Gates::kRingDepth, its hull and a clearance, kArrivalClearance, beyond the gate's centre), so
//     the gate does not see it pass through; and clear of every body already there and of what has
//     come out before it in the step: there if that is clear, or else at the first place clear on
//     rings about it, across the way the gate faces, each ring a hull's breadth and the clearance
//     further out, with six more places round each, so a ship that came out a moment ago, or one
//     flying into the gate the other way, is not met by the next;
//   - its Physics::FlightControls are taken against nothing, since what they were taken against is
//     a system away;
//   - a LaneLeft is raised.
// Entities arrive in entity order. Physics removes a body as its entity enters a lane, in the step
// the command is applied in, which ends every contact it had.
//
// The galaxy and its star systems are definitions, the game's start-up as physics' are, and not
// state: a save names systems by identifier and jump points by name, and a world it is loaded into
// is given the same galaxy. A lane's transit is whole steps, rounded up, and at least one.
//
// The world holds it from Attach on, through the systems and commands it registers.
class LaneTravel
{
  public:
	static constexpr std::string_view kSystemName = "lanes";
	// Metres kept between the hull of a body coming out of a gate and every other's: a steered
	// ship's clearance (Ai::Steered).
	static constexpr double kArrivalClearance = 20.0;

	// Registers OnLane as a component, the EnterLane command and the "lanes" system, ordered after
	// physics', so physics has to be attached first (Physics::PhysicsWorld::Attach), whose shapes
	// say how far apart arrivals come out. Throws std::logic_error as SimWorld's registrations do,
	// for one already made.
	static LaneTravel &Attach(Sim::SimWorld &world, const Physics::PhysicsWorld &physics);

	// The galaxy the lanes are; resolved, as reading one does. Until it is given, every EnterLane is
	// refused.
	void SetGalaxy(Celestial::Galaxy galaxy);
	const Celestial::Galaxy *Galaxy() const
	{
		return m_galaxy ? &*m_galaxy : nullptr;
	}
	// A star system, for where its jump points are as an entity arrives. It has to be resolved;
	// throws std::invalid_argument for a system already defined. An EnterLane to a system not
	// defined is refused.
	void DefineStarSystem(Celestial::StarSystem system);
	bool HasStarSystem(std::uint64_t system) const;

  private:
	explicit LaneTravel(const Physics::PhysicsWorld &physics) : m_physics{physics}
	{
	}

	void Enter(Sim::SimWorld &world, const EnterLane &command) const;
	void Arrive(Sim::SimWorld &world) const;

	const Physics::PhysicsWorld &m_physics;
	std::optional<Celestial::Galaxy> m_galaxy;
	// By Sim::SystemId; looked up, and ordered regardless.
	std::map<std::uint64_t, Celestial::StarSystem> m_systems;
};
} // namespace Jettison::Ai
