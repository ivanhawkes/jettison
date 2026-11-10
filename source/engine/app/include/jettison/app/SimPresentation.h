#pragma once

#include <entt/entt.hpp>

#include <jettison/sim/Snapshot.h>

namespace Jettison::App
{
// A presentation entity that shows a simulation entity. Presentation holds the simulation
// entity's identifier, never a reference into the world, and finds it in the snapshots.
struct SimProxyComponent
{
	entt::entity simEntity{entt::null};
};

// The sync phase: writes each proxy's simulation transform, alpha of the way from previous to
// current, into its TransformComponent. The position stays in double precision, in the system
// frame; it becomes a float only when the renderer takes the camera's position off it
// (docs/architecture.md, "Coordinate frames and units"). A proxy whose simulation entity has no
// transform is left where it was: the events that say why are presentation's to act on.
void SyncSimProxies(
    entt::registry &presentation, const Sim::Snapshot &previous, const Sim::Snapshot &current,
    double alpha);
} // namespace Jettison::App
