#pragma once

#include <entt/entt.hpp>

#include <jettison/sim/Snapshot.h>

namespace Jettison::Scene
{
// Presentation's proxies of the simulation's entities (SimProxyComponent), from its snapshots.

// The sync phase's half for the simulation: writes each proxy's simulation transform, alpha of the
// way from previous to current, into its TransformComponent. The position stays in double
// precision, in its frame (Sim::InFrame); it becomes a float only when the renderer takes the camera's
// position off it (docs/coordinates.md). A proxy whose simulation
// entity has no transform is left where it was: the events that say why are presentation's to act
// on.
void SyncSimProxies(
    entt::registry &presentation, const Sim::Snapshot &previous, const Sim::Snapshot &current,
    double alpha);
} // namespace Jettison::Scene
