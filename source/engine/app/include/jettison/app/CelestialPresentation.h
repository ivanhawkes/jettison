#pragma once

#include <entt/entt.hpp>

#include <jettison/celestial/StarSystem.h>

namespace Jettison::App
{
// Every celestial body the scene names, checked against the star system it came with. Throws
// std::runtime_error naming the first body the system does not have, at load, so a misspelt name
// is a start-up error rather than a body that never moves.
void ResolveCelestialBodies(const entt::registry &presentation, const Celestial::StarSystem &system);

// The sync phase's half for bodies on rails: each entity with a CelestialBodyComponent is placed
// where its body is at simSeconds, and turned as the body spins, through a PlacementComponent, so
// the authored transform is untouched. The time is the presentation time between two steps
// (Celestial::PresentationTime): every body shown is shown at the same moment, on its orbit.
void SyncCelestialBodies(
    entt::registry &presentation, const Celestial::StarSystem &system, double simSeconds);
} // namespace Jettison::App
