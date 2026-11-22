#pragma once

#include <entt/entt.hpp>

#include <functional>

#include <jettison/sim/Snapshot.h>

namespace Jettison::Scene
{
// Presentation's proxies of the simulation's entities (SimProxyComponent), from its snapshots.

// The sync phase's half for the simulation: writes each proxy's simulation transform, alpha of the
// way from previous to current, into its TransformComponent. The position stays in double
// precision, in its frame (Sim::InFrame); it becomes a float only when the renderer takes the camera's
// position off it (docs/architecture.md, "Coordinate frames and units"). A proxy whose simulation
// entity has no transform is left where it was: the events that say why are presentation's to act
// on.
void SyncSimProxies(
    entt::registry &presentation, const Sim::Snapshot &previous, const Sim::Snapshot &current,
    double alpha);

// Makes the presentation of a simulation entity the scene has no proxy for: the game's to supply,
// since only it knows what a vehicle looks like. Returns the proxy's root, which the caller then makes
// the entity's proxy, or null to leave the entity unshown while it stays in the snapshots.
using ProxyFactory = std::function<entt::entity(entt::registry &presentation, entt::entity simEntity)>;

} // namespace Jettison::Scene
