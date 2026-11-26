#pragma once

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/dock/Definitions.h>
#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Serialise
{
class BinaryRecord;
template <typename Data> class Migrations;
} // namespace Jettison::Serialise

namespace Jettison::Sim
{
class DefinitionRegistry;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::Dock
{
// Platforms' berths and the vehicles on them, in the simulation (docs/docks.md). Who
// holds a berth is authoritative state every player sees the same, so berths are entities of their
// platforms, saved, and given by commands in the order the queue applies them. What a berth costs, who
// may land where and what loitering on one means are the game's; this is what any game's platforms
// would want unchanged.

// A vehicle's kind, by definition name (VehicleDefinition): what berth it fits. Its Physics::Body
// names the vehicle's shape (Docks::VehicleShape) for it to collide as its size. Registered with
// Reflect, its field Saved, by RegisterTypes.
struct Vehicle
{
	std::string definition;

	bool operator==(const Vehicle &) const = default;
};

// A platform built to a layout: its berths, in their numbers' order, and the vehicles waiting for one,
// first come first. Registered with Reflect, every field Saved, by RegisterTypes; Docks::Attach
// registers it with its chain (Migrations).
struct Platform
{
	std::string layout;
	std::vector<entt::entity> berths;
	std::vector<entt::entity> queue;

	bool operator==(const Platform &) const = default;

	// Its storage's chain: the current version alone until player testing (docs/decisions.md, "Formats
	// change in place until player testing").
	static const Serialise::Migrations<Serialise::BinaryRecord> &Migrations();
};

enum class BerthState : std::uint8_t
{
	Free,
	// Given to a vehicle, until it lands or the reservation lapses.
	Reserved,
	// A vehicle is clamped to it.
	Holding
};

// A berth, an entity of its platform's, on the platform's rails at its place on the deck
// (Physics::OnRails) and in its frame, so a vehicle's destination by it turns and moves with the
// platform. The vehicle it is reserved for or holds, and the step its reservation lapses at
// (Time::SimClock::Step). Registered with Reflect, every field Saved, by RegisterTypes; Docks::Attach
// registers it with its chain (Migrations).
struct Berth
{
	entt::entity platform{entt::null};
	std::uint32_t number{0};

	// Its berth class's name (BerthClassDefinition).
	std::string berthClass;
	bool display{false};
	BerthState state{BerthState::Free};
	entt::entity vehicle{entt::null};
	std::uint64_t lapses{0};

	bool operator==(const Berth &) const = default;

	// Its storage's chain: the current version alone until player testing (docs/decisions.md, "Formats
	// change in place until player testing").
	static const Serialise::Migrations<Serialise::BinaryRecord> &Migrations();
};

enum class Stage : std::uint8_t
{
	// Waiting in the platform's queue for a berth.
	Queued,
	// Given a berth and on its way to the point above it: round the platform's sphere to a point
	// further out along the berth's normal, stopped there, then straight down the normal.
	Inbound,
	// Coming down onto the berth from above it.
	Descending,
	// Clamped to the berth: held in the platform's frame, on its rails, out of flight.
	Docked,
	// Let go and lifting: flown to the point above the berth, clear of the platform's sphere, or for a
	// vehicle its pilot flies (ReleaseBerth::fly), lifted kLiftHeight off the berth. It gives the berth
	// up once there.
	Leaving
};

// A vehicle's business with a platform: its place in the queue, or its berth and how far it has got. A
// vehicle flown by the docks (fly) is steered through each stage; one that is not is the game's or the
// pilot's to fly, and is clamped once it is on its berth as one flown would be. The feature it asked
// for (RequestBerth::feature) is kept, so the queue gives it only a berth that offers it. Registered
// with Reflect, every field Saved, by RegisterTypes; Docks::Attach registers it with its chain
// (Migrations).
struct Docking
{
	entt::entity platform{entt::null};
	entt::entity berth{entt::null};
	Stage stage{Stage::Queued};
	bool fly{true};
	std::string feature{};

	bool operator==(const Docking &) const = default;

	// Its storage's chain: the current version alone until player testing (docs/decisions.md, "Formats
	// change in place until player testing").
	static const Serialise::Migrations<Serialise::BinaryRecord> &Migrations();
};

bool IsDocked(const entt::registry &registry, entt::entity vehicle, entt::entity platform);

// The command that asks a platform for a berth for a vehicle, applied in the order commands are: the
// lowest-ranked free berth its vehicle fits, the lowest numbered of those, or, with none, a place at
// the back of the platform's queue. Flown in by the docks if fly is set. Refused, with a BerthRefused,
// for a sender that does not control the vehicle (Sim::MayCommand); a vehicle that is not a dynamic
// body with a Vehicle, or is in another frame or local frame than the platform; one that already holds
// a berth or a place; and a platform that is not one, or has no berth the vehicle fits; then by the
// game's rules (Control). With a feature (BerthLayout::features), only a berth that offers it is given,
// the lowest-ranked and lowest numbered of those, and a platform with no such berth the vehicle fits
// refuses; with none, any berth. Registered with Reflect, every field Saved, by RegisterTypes.
struct RequestBerth
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
	bool fly{true};
	std::string feature{};
};

// The command that gives up a vehicle's berth: its place in the queue, or its reservation, which frees
// the berth for the next vehicle; or, docked, it is let go and lifted clear (Stage::Leaving), and gives
// the berth up once there. A vehicle the docks fly (fly) is flown out to the point above its berth,
// clear of the platform's sphere, as a vehicle that goes on by steering or in transit needs; one its
// pilot flies is lifted straight off the berth by its thrusters, kLiftHeight, and is the pilot's in a
// few seconds rather than the half a minute the climb out of a large platform's sphere takes. Refused
// for a sender that does not control the vehicle, and for a vehicle with nothing to give up. Registered
// with Reflect, every field Saved, by RegisterTypes.
struct ReleaseBerth
{
	Sim::Controller sender;
	entt::entity vehicle{entt::null};
	bool fly{true};
};

// How far a vehicle its pilot flies is lifted off its berth, from where it rests, before the berth is
// given up and the controls are the pilot's: clear of the clamps and of a vehicle on the next berth,
// and at the lift's 20 m/s about three seconds from the clamps opening.
inline constexpr double kLiftHeight = 40.0;

// Why a berth is refused or not given up (Sim::Reason), each worded by the string tables under
// reason.dock.<name>; the arguments, where there are any, follow. Registered with Reflect as
// "Dock.Reason" by RegisterTypes.
enum class Reason : std::uint8_t
{
	VehicleGone,
	NotController,
	NotAPlatform,
	NoVehicle,
	CannotFly,
	// The platform.
	OtherFrame,
	// The platform.
	OtherLocalFrame,
	AlreadyQueued,
	// The berth's number, and the platform.
	AlreadyHoldsBerth,
	// The platform.
	NoBerthFits,
	HoldsNothing,
	AlreadyLeaving,
	// The platform.
	NoBerthOffers
};

// The answers, raised as events (Sim::EventQueue): a berth given, a place in the queue (from 1), a
// refusal and why, a reservation lapsed, a vehicle clamped to its berth and let go of it, and a berth
// or a place given up, or left behind once a vehicle has lifted clear. Registered with Reflect, every
// field Saved, by RegisterTypes.
struct BerthGiven
{
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
	entt::entity berth{entt::null};
	std::uint32_t number{0};
};
struct BerthQueued
{
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
	std::uint32_t place{0};
};
struct BerthRefused
{
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
	Sim::Reason reason;
};
struct BerthLapsed
{
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
	entt::entity berth{entt::null};
	std::uint32_t number{0};
};
struct BerthReleased
{
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
};
struct Landed
{
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
	entt::entity berth{entt::null};
	std::uint32_t number{0};
};
struct Left
{
	entt::entity vehicle{entt::null};
	entt::entity platform{entt::null};
	entt::entity berth{entt::null};
	std::uint32_t number{0};
};

// A game's own rules for who docks (docs/docks.md, "Dock control a game decides"). The engine has no
// rule of admission or rank: with neither set, every request that fits is admitted and the queue is
// served first come, first served. Both are the game's start-up, as definitions are, and read the world
// without changing it, so a step decides the same on every run as long as the rules read only saved
// state.
//
// admit is asked once a request has passed the docks' own checks, and a reason refuses it with a
// BerthRefused carrying that reason. rank is asked of every vehicle in a platform's queue each time the
// queue is served, and as a vehicle is queued for its place: the higher served first, equal ranks in
// the order they asked. Docks::Park, a world's initial state, asks neither.
struct Control
{
	std::function<std::optional<Sim::Reason>(const Sim::SimWorld &world, const RequestBerth &request)>
	    admit;
	std::function<std::int64_t(const Sim::SimWorld &world, entt::entity platform, entt::entity vehicle)>
	    rank;
};

// The docks in a simulation world: the components, the commands, and the system "dock"
// (kSystemName), after physics' ("physics") and before a game's that order themselves after it, which
// each step, in entity order:
//   1. Gives up what a vehicle can no longer hold: a berth or a place whose vehicle has gone, or has
//      left its platform's frame or local frame, and a vehicle's business with a platform that has
//      gone.
//   2. Lapses each reservation whose step has come, freeing the berth.
//   3. Moves each vehicle on: one on its way to its berth, within a clamp's reach of it (2 m across it,
//      a metre above or below the vehicle's resting height), turned to it (its top within 10° of the
//      normal, its nose within 15° of the heading), and moving at under half a metre a second, is
//      clamped there, docked; one flown in that has reached the point above its berth comes down onto
//      it; and one leaving that has reached the point above its berth gives it up.
//   4. Serves each platform's queue in the game's rank (Control) and then the order they asked,
//      each vehicle the lowest-ranked free berth it fits.
//
// A vehicle flown in by the docks is steered (Ai::Steering): first along a course round the platform's
// sphere (Ai::Waypoints) to a point out along its berth's normal, as far beyond the point above the
// berth as it stops in from its top speed, stopping there; then straight down the normal to the point
// above its berth, outside that sphere; then down the normal onto the berth, kept clear of the platform
// by its shape (Ai::Destination::byShape), turned to the berth, slowing to stop a hand's breadth
// above it; and once clamped its Physics::Body is kinematic, on the platform's rails at its place on
// the berth. Undocking makes it dynamic again and lifts it the same way back up.
//
// The definitions are the game's start-up, as physics' shapes are, and not state: a save names
// layouts and vehicles, and a world it is loaded into is given the same ones.
class Docks
{
  public:
	static constexpr std::string_view kSystemName = "dock";

	// Registers the components, the commands and the system, ordered after physics', so physics
	// has to be attached first, and steering, whose destinations the docks set; and shows the berths
	// in the snapshots. Throws std::logic_error as SimWorld's registrations do, for one already made.
	static Docks &Attach(Sim::SimWorld &world, Physics::PhysicsWorld &physics);

	// The physics shapes a vehicle's and a layout's bodies name, "vehicles/<name>" and
	// "platforms/<name>", as saves have them.
	static std::string VehicleShape(std::string_view vehicle);

	static std::string PlatformShape(std::string_view layout);

	// A berth class, checked (CheckBerthClass), which vehicles and layouts defined after it may name.
	// Throws Serialise::FormatError for one that does not fit, and std::invalid_argument for a name
	// already defined.
	void DefineBerthClass(std::string name, BerthClassDefinition berthClass);

	// A vehicle and a layout, checked (CheckVehicle, CheckLayout) against the berth classes defined,
	// and their shapes defined in physics. Throws Serialise::FormatError for one that does not fit, and
	// std::invalid_argument for a name already defined.
	void DefineVehicle(std::string name, VehicleDefinition vehicle);

	void DefineLayout(std::string name, PlatformLayout layout);

	// Every BerthClassDefinition, VehicleDefinition and PlatformLayout the registry has loaded, in that
	// order.
	void Define(const Sim::DefinitionRegistry &definitions);

	const BerthClassDefinition *FindBerthClass(std::string_view name) const;

	const BerthClasses &DefinedBerthClasses() const
	{
		return m_berthClasses;
	}

	// Dock::Fits over the berth classes defined.
	bool Fits(const VehicleDefinition &vehicle, std::string_view berthClass) const;

	// Whether the berth offers the feature, by its place in its platform's layout
	// (BerthLayout::features): a definition's answer, so a berth saves none.
	bool Offers(const entt::registry &registry, entt::entity berth, std::string_view feature) const;

	const VehicleDefinition *FindVehicle(std::string_view name) const;

	const PlatformLayout *FindLayout(std::string_view name) const;

	// A platform's entities in the world, built to the layout and placed where the game places it: on
	// the rails of the frame's body of that name, which the game's frame model moves
	// (Physics::FrameModel), and in that body's local frame; a kinematic body colliding as its layout's
	// decks and piers, and an entity for each berth, on the same rails at its place. All of it placed
	// where the rails have it now. The frame and its body have to be defined in physics; throws
	// std::invalid_argument for a layout not defined.
	entt::entity MakePlatform(
	    Sim::SimWorld &world, std::uint64_t frame, std::string_view body, std::string_view layout);

	// A vehicle put on a berth and clamped there, docked, as a game parks one at its start: a dynamic
	// body with a Vehicle that fits the berth, in the platform's frame. Throws std::invalid_argument
	// for a berth that is not free, a display berth among them, or a vehicle that cannot be put on it.
	void Park(Sim::SimWorld &world, entt::entity vehicle, entt::entity berth);

	// Replaces the game's rules for who docks; a request or a queue served after it follows them.
	void SetControl(Control control);

  private:
	explicit Docks(Physics::PhysicsWorld &physics) : m_physics{physics}
	{
	}

	void Request(Sim::SimWorld &world, const RequestBerth &command);

	void Release(Sim::SimWorld &world, const ReleaseBerth &command);

	void Step(Sim::SimWorld &world);

	// The step the docks next have to run in (Sim::Quiet), each of Step's parts asked whether it
	// would change anything: none while every vehicle is docked or queued for a berth none is free for
	// and no reservation is held, until the first lapses.
	std::uint64_t QuietFrom(const Sim::SimWorld &world) const;

	// The lowest-ranked free berth the vehicle fits, the lowest numbered of those, that offers the
	// feature the vehicle asked for if it asked for one (Docking::feature), or none.
	entt::entity FreeBerthFor(
	    const entt::registry &registry, const Platform &platform, entt::entity vehicle) const;

	// Gives each vehicle in the platform's queue, from the front, the lowest-ranked free berth it fits.
	// The time is the one the world's transforms are at: the step's start in a command, its end in the
	// system.
	void Serve(Sim::SimWorld &world, entt::entity platform, double time);

	// The platform's queue in the order it is served: by the game's rank (Control::rank), the higher
	// first, and in the order they asked within a rank.
	std::vector<entt::entity> Ranked(const Sim::SimWorld &world, entt::entity platform) const;

	void Give(Sim::SimWorld &world, entt::entity vehicle, entt::entity berth, double time);

	// Frees the vehicle's berth or its place, lets it go if it is clamped, takes away its business with
	// the platform and, if the docks flew it, its destination; and says so if asked to.
	void GiveUp(Sim::SimWorld &world, entt::entity vehicle, bool announce);

	// The destinations of each stage the docks fly a vehicle through.
	void FlyInbound(Sim::SimWorld &world, entt::entity vehicle, double time);

	void FlyAbove(Sim::SimWorld &world, entt::entity vehicle);

	void FlyDown(Sim::SimWorld &world, entt::entity vehicle);

	void FlyUp(Sim::SimWorld &world, entt::entity vehicle);

	// A vehicle its pilot flies, lifted off its berth by its own thrusters, straight up at the lift's
	// speed against the platform; and whether it is kLiftHeight off it.
	static void Lift(entt::registry &registry, entt::entity vehicle);

	bool Lifted(const entt::registry &registry, entt::entity vehicle) const;

	bool InClampReach(const entt::registry &registry, entt::entity vehicle) const;

	void Clamp(Sim::SimWorld &world, entt::entity vehicle, double time);

	void Unclamp(Sim::SimWorld &world, entt::entity vehicle);

	// The berth's place in its platform's layout.
	const BerthLayout &PlaceOf(const entt::registry &registry, const Berth &berth) const;

	const VehicleDefinition *VehicleOf(const entt::registry &registry, entt::entity vehicle) const;

	// How high above the berth, along its normal, a vehicle clears the platform's sphere, its own
	// shape and its clearance beyond that: the point it comes down from and lifts to.
	double AboveHeight(const entt::registry &registry, entt::entity vehicle, const Berth &berth) const;

	// Where a vehicle of the definition rests on the berth: its centre half its height above the deck,
	// in the berth's axes.
	static double RestingHeight(const VehicleDefinition &vehicle);

	Physics::PhysicsWorld &m_physics;
	BerthClasses m_berthClasses;
	std::map<std::string, VehicleDefinition, std::less<>> m_vehicles;
	std::map<std::string, PlatformLayout, std::less<>> m_layouts;
	Control m_control;
};
} // namespace Jettison::Dock
