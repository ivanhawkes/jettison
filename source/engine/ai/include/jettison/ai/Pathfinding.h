#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace Jettison::Ai
{
// A* over a directed graph with non-negative edge costs: the jump-lane graph between systems and
// the waypoint graphs within one. Nodes are indices; the graph is whatever the caller holds, read
// through the two callbacks. Deterministic: ties break on node index. Filled in phase 9.
using NodeId = std::uint32_t;

struct Edge
{
	NodeId to{0};
	double cost{0.0};
};

struct Graph
{
	std::vector<std::vector<Edge>> adjacency;
};

// The node sequence from start to goal inclusive, or nothing when the goal is unreachable.
std::optional<std::vector<NodeId>> FindPath(const Graph &graph, NodeId start, NodeId goal);
} // namespace Jettison::Ai
