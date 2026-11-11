#pragma once

#include <entt/entt.hpp>

#include <string_view>

namespace Jettison::Physics
{
class PhysicsWorld;
}

namespace Jettison::Sim
{
class EventQueue;
class SimWorld;
} // namespace Jettison::Sim

namespace Jettison::App
{
// Docking as the shell plays it, standing in for a game's (docs/architecture.md, "Physics"): a
// station's docking port is a sensor before its dock, and a ship in it, slow enough relative to the
// station, is docked until it leaves. The engine's part is the sensor and the contacts it reports;
// what docking means is the game's, so this is the smallest version that shows it working.

// The sensor's entity: the station it is the port of, and how slowly a ship has to be moving
// relative to it, in metres a second, to dock. Simulation state, registered with Reflect, every
// field Saved.
struct DockingPort
{
	entt::entity station{entt::null};
	float maxSpeed{2.0f};

	bool operator==(const DockingPort &) const = default;
};

// A ship that is docked, and at which port. Simulation state, registered with Reflect, its field
// Saved.
struct Docked
{
	entt::entity port{entt::null};

	bool operator==(const Docked &) const = default;
};

// Raised for presentation as a ship docks and as it leaves (Sim::EventQueue).
struct ShipDocked
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
};
struct ShipUndocked
{
	entt::entity ship{entt::null};
	entt::entity station{entt::null};
};

inline constexpr std::string_view kDockingSystemName = "shell.docking";

// Registers the types with Reflect and the components with the world, and the system "shell.docking"
// after "physics", which reads what physics left. The physics world is the simulation world's.
void RegisterDocking(Sim::SimWorld &world, const Physics::PhysicsWorld &physics);

// One step: a ship (an entity with Physics::FlightControls) touching a port, undocked, and moving no
// faster than the port allows relative to its station, docks; a docked ship no longer touching its
// port, or whose port is gone, undocks. Both are raised as events. Speeds are relative to the
// ship's frame, which is the station's: a ship and a station in different frames do not touch.
void StepDocking(
    entt::registry &registry, const Physics::PhysicsWorld &physics, Sim::EventQueue &events);
} // namespace Jettison::App
