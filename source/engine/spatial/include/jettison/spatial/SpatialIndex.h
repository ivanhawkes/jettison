#pragma once

#include <jettison/spatial/LooseOctree.h>
#include <jettison/spatial/SortedGrid.h>

namespace Jettison::Spatial
{
// The per-system index over simulation entities for the queries the simulation and AI make:
// nearest station, ships within sensor range, everything in a volume. Separate from Jolt's
// broadphase, which serves physics. Positions are system-frame doubles, and results never depend on
// the structure's internals (Queries.h).
//
// Phase 6c measures the two structures against each other and keeps one.
using SpatialIndex = LooseOctree;
} // namespace Jettison::Spatial
