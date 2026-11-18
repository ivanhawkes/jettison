#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <span>
#include <string_view>

#include <jettison/sim/Controller.h>
#include <jettison/sim/Reason.h>

namespace Jettison::Dock
{
class Docks;
}
namespace Jettison::Physics
{
class PhysicsWorld;
struct ContactEvent;
} // namespace Jettison::Physics
namespace Jettison::Sim
{
class SimWorld;
}

namespace Jettison::Sample
{
// Damage and its repair, the sample's smallest: a hull that hits something hard is damaged by the
// speed it hit at, and a ship docked on a pad that offers a dry dock (a pad's features,
// Dock::PadLayout, the sample's kDryDock) is repaired for its owner's credits. The rule and its
// numbers are the sample's; the engine's part is the contacts, the docks and the pad's feature.
// Damage changes nothing about how a ship flies or trades: it is state to repair, which is what
// the dry dock is exercised by.

// A hull's damage, in points: whole metres a second of the speed of each hard contact it has
// taken, summed. A ship with none has no Damage. Simulation state, registered with Reflect as
// "Shell.Damage", its field Saved, and shown in snapshots.
struct Damage
{
	std::int64_t points{0};

	bool operator==(const Damage &) const = default;
};

// The command: the ship's damage is repaired, all of it, at kRepairPrice a point from its owner's
// purse. Refused, with a RepairRefused, from a sender that does not command the ship; for a ship
// that has no owner or no damage, or is not docked on a pad that offers a dry dock; and for a
// repair that costs more than the purse holds. Registered with Reflect as "Shell.Repair".
struct Repair
{
	Sim::Controller sender;
	entt::entity ship{entt::null};
};

// Raised as a repair is done: the points repaired, what they cost and the owner's credits after.
struct Repaired
{
	entt::entity ship{entt::null};
	std::int64_t points{0};
	std::int64_t cost{0};
	std::int64_t credits{0};
};

// The reason is one of the sample's (TradeReason, Trading.h).
struct RepairRefused
{
	entt::entity ship{entt::null};
	Sim::Reason reason;
};

// The feature of a pad that repairs.
inline constexpr std::string_view kDryDock = "dry-dock";
// The slowest contact that damages, in metres a second as the bodies closed: a ship set down on its
// pad by the docks meets it at well under a metre a second.
inline constexpr float kDamagingSpeed = 5.0f;
// Credits a point of damage.
inline constexpr std::int64_t kRepairPrice = 100;
inline constexpr std::string_view kDamageSystemName = "shell.damage";

// What a step's contacts do: each that began at kDamagingSpeed or more, and was not a sensor's,
// adds its speed in whole metres a second to each of its two bodies that has a hull (Dock::Hull).
void TakeDamage(entt::registry &registry, std::span<const Physics::ContactEvent> contacts);

// Registers the types with Reflect, Damage with the world (shown in snapshots), the Repair command
// and the system "shell.damage", which runs TakeDamage on each step's contacts after physics. The
// physics world and the docks are the world's own services, which outlive its systems.
void RegisterRepair(
    Sim::SimWorld &world, const Physics::PhysicsWorld &physics, const Dock::Docks &docks);
} // namespace Jettison::Sample
