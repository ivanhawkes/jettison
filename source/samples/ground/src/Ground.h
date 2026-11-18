#pragma once

#include <entt/entt.hpp>
#include <glm/vec3.hpp>

#include <cstdint>

#include <jettison/sim/Controller.h>

namespace Jettison::Physics
{
class PhysicsWorld;
}
namespace Jettison::Sim
{
class SimWorld;
}

namespace Jettison::Sample
{
// The ground sample's world (docs/sample-game.md): one frame under a planet's gravity, a static slab
// of ground in it and a rover on the slab, driven by the flight model with no vertical thrust, so it
// stays on the ground and turns only about its up axis.

// Half of each of the rover's sides, its shape's and its proxy's: 2 m wide, 0.8 m high and 3 m long.
inline constexpr glm::vec3 kRoverHalfExtents{1.0f, 0.4f, 1.5f};

// The frame's identifier (Sim::FrameId("ground")).
std::uint64_t GroundFrame();

// The ground's and the rover's shapes and the frame's gravity, as the world is made: a save names
// them, and the world it is loaded into has them.
void DefineGround(Physics::PhysicsWorld &physics);

// The ground and the rover at rest on it, the player at its helm; returns the rover.
entt::entity MakeGround(Sim::SimWorld &world, Sim::Controller player);

// The rover, the one entity the world has with Physics::Handling, or null for none.
entt::entity FindRover(const entt::registry &world);
} // namespace Jettison::Sample
