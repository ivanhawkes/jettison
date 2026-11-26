#pragma once

#include <entt/entt.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Transit
{
// A portal, any game's (docs/transit.md): a door between two levels, a teleporter,
// a way from one frame to another faster than anything could go. The entity leaves its frame for a
// set time, in none meanwhile (Sim::kNoFrame), and comes out at a named exit the game places, in
// any frame, the one it left included. Portals runs portals kept in an InPortal; a game whose
// portals save fields of their own keeps its own component and builds on the parts below.

// Where an exit is as the step ends, in its frame, facing the way an arrival comes out (its -z); the
// local frame an arrival with a Physics::Body comes out in (Physics::LocalFrame), or none for the
// frame's own; and how far ahead of the exit an arrival's shape comes out, past a door's frame or
// what would see it go through again.
struct Exit
{
	Sim::Transform place;
	std::string localFrame;
	double ahead{0.0};
};

// Sends an entity through a portal: in no frame until it comes out, and its Destination given up
// (GiveUpDestination), since that is in the frame it leaves. It keeps its Physics::Body, whose body
// physics removes in the step it leaves in.
void Depart(entt::registry &registry, entt::entity entity);

// The entities that come out of a portal with the step ending at that count, in entity order: those
// whose kept component (InPortal, or the game's own) has an arrivalStep no later.
template <typename Kept>
std::vector<entt::entity> Arriving(const entt::registry &registry, std::uint64_t ending)
{
	std::vector<entt::entity> arriving;
	for (const auto [entity, kept] : registry.view<const Kept>().each())
		if (kept.arrivalStep <= ending)
			arriving.push_back(entity);
	std::ranges::sort(arriving, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	return arriving;
}

// The first step that brings an entity out, for a portal system's quiet (Sim::Quiet::next): until
// then, nothing in a portal changes.
template <typename Kept> std::uint64_t NextArrival(const Sim::SimWorld &world)
{
	std::uint64_t next = std::numeric_limits<std::uint64_t>::max();
	for (const auto [entity, kept] : world.Registry().view<const Kept>().each())
		next = std::min(next, kept.arrivalStep - std::min<std::uint64_t>(kept.arrivalStep, 1));
	return next;
}

// Brings each arriving entity out at its exit, in its frame, in the order given: its Transform at
// the exit and its Sim::InFrame the frame; with a Physics::Body, ahead of the exit by its ahead, the
// sphere that holds the entity's shape and the clearance, and clear of every body already in those
// frames and of each arrival before it (ClearPlace), at rest in the exit's local frame, which
// physics makes its body in the next step; and its Physics::FlightControls taken against nothing,
// since what they were taken against is in the frame it left.
void ComeOut(
    entt::registry &registry, const Physics::PhysicsWorld &physics,
    std::span<const entt::entity> arriving, std::span<const std::uint64_t> frames,
    std::span<Exit> exits, double clearance);

// An entity in a portal: in no frame from the step it entered to the step it comes out in, its
// Transform where it left. Registered with Reflect, every field Saved, by RegisterTypes.
struct InPortal
{
	// The frame it left (Sim::FrameId) and the entrance it went in by.
	std::uint64_t from{0};
	std::string entrance;

	// The frame it comes out in, the one it left or another, and the exit it comes out of.
	std::uint64_t to{0};
	std::string exit;

	// The clock's step count once it has come out (Time::SimClock::Step): it comes out as the step
	// before that one ends.
	std::uint64_t arrivalStep{0};

	bool operator==(const InPortal &) const = default;
};

// Raised as an entity comes out of a portal. Registered with Reflect, every field Saved, by
// RegisterTypes.
struct PortalLeft
{
	entt::entity entity{entt::null};
	std::uint64_t frame{0};
	std::string exit;
};

// Portals in a simulation world: the InPortal component and the system "portals" (kSystemName),
// which runs after physics' and brings out each entity whose time in its portal ends with the step,
// at its exit as the game's Locate places it as the step ends (ComeOut, kArrivalClearance), and
// raises a PortalLeft. Entities come out in entity order, and the steps before are passed over
// (Sim::Quiet).
//
// The world holds it from Attach on, through the system it registers.
class Portals
{
  public:
	static constexpr std::string_view kSystemName = "portals";

	// Metres an arrival keeps from what is already there.
	static constexpr double kArrivalClearance = 1.0;

	// Where an exit in a frame is at a time. The game's, a definition and not state, the same every
	// run; it throws std::invalid_argument for an exit it does not have.
	using Locate = std::function<Exit(std::uint64_t frame, std::string_view exit, double time)>;

	// Registers the component and the "portals" system, ordered after physics, given the physics
	// world whose shapes say how far apart arrivals come out, and the game's exits. Throws
	// std::invalid_argument for no Locate, and std::logic_error as SimWorld's registrations do, for
	// one already made.
	static Portals &Attach(Sim::SimWorld &world, const Physics::PhysicsWorld &physics, Locate locate);

	// Sends an entity in a frame through the entrance, to come out of the exit in that frame after
	// that many seconds (Steps), from this step (Depart). Throws std::invalid_argument for an entity
	// in no frame, one in a portal already among them.
	static void Enter(
	    Sim::SimWorld &world, entt::entity entity, std::string entrance, std::uint64_t to,
	    std::string exit, double seconds);

  private:
	Portals(const Physics::PhysicsWorld &physics, Locate locate)
	    : m_physics{physics}, m_locate{std::move(locate)}
	{
	}

	void Arrive(Sim::SimWorld &world) const;

	const Physics::PhysicsWorld &m_physics;
	Locate m_locate;
};
} // namespace Jettison::Transit
