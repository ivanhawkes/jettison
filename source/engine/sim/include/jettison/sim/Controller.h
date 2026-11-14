#pragma once

#include <entt/entt.hpp>

#include <cstdint>

namespace Jettison::Sim
{
// Who sends a command and who commands an entity (docs/roadmap.md, "Decided": multiplayer in time,
// single-player first). A command for a ship carries its sender, and the simulation refuses it from
// a sender that does not control the ship, so a server can hold every player's commands in one
// queue; nothing in a simulation module assumes there is one player.

enum class ControllerKind : std::uint8_t
{
	// No one in particular: a command the game's own rules send, or a test's, which commands any
	// entity.
	None,
	// A player, by the slot they play in.
	Player,
	// The simulation's own agents, a trader's behaviour among them.
	Ai
};

// Registered with Reflect, both fields Saved, by RegisterTypes.
struct Controller
{
	ControllerKind kind{ControllerKind::None};
	std::uint32_t slot{0};

	bool operator==(const Controller &) const = default;
};

// Who commands an entity. An entity without one takes any sender's commands, as a ship had before
// commands carried a sender. The game registers it as a component, as it does Transform. Registered
// with Reflect, its field Saved, by RegisterTypes.
struct ControlledBy
{
	Controller controller;

	bool operator==(const ControlledBy &) const = default;
};

// Whether a command from the sender may command the entity: one with no ControlledBy, or the sender
// is its controller, or the sender is no one in particular (ControllerKind::None), as the game's own
// rules are.
bool MayCommand(const entt::registry &registry, entt::entity entity, const Controller &sender);

// Who is at an entity's helm (docs/roadmap.md, "Decided": a player has a fleet): the one flying it by
// its controls, which it takes from them alone (Physics::SetFlightControls), and for whom its
// behaviour stands down (Ai::Behaviours). Taken and left by Ai::TakeHelm and Ai::LeaveHelm, by one
// that commands the entity, each at one helm at a time. Registered with Reflect, its field Saved, by
// RegisterTypes.
struct Helm
{
	Controller controller;

	bool operator==(const Helm &) const = default;
};

// Whether flight controls from the sender may fly the entity: the sender is at its helm, or is no
// one in particular, as the game's own rules are. An entity no one is at the helm of is flown by
// what the game's rules give it (its behaviour's steering, the docks), and by no player's controls.
bool MayFly(const entt::registry &registry, entt::entity entity, const Controller &sender);
} // namespace Jettison::Sim
