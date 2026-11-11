#pragma once

#include <jettison/spatial/Octree.h>

namespace Jettison::Spatial
{
// The per-system index over simulation entities for the queries the simulation and AI make:
// nearest station, ships within sensor range, everything in a volume. Separate from Jolt's
// broadphase, which serves physics. Positions are system-frame doubles, and results never depend on
// the structure's internals (Queries.h).
//
// An octree, chosen in phase 6c over a sorted grid by measurement over a system's traffic
// (tests/SpatialBenchmark.cpp; docs/roadmap.md, "Decided"). At 100,000 ships it was faster on every
// query but the 500 km sensor sweep, by up to 25 times for a nearest query from open space, which a
// grid answers by growing a search through empty cells, and cheaper to update each step.
using SpatialIndex = Octree;
} // namespace Jettison::Spatial
