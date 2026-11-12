#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace Jettison::Ai
{
// A* over a directed graph with non-negative edge costs: the jump points of the galaxy and the
// lanes between them (Routes.h, PlotRoute), and the waypoints of a course across one system
// (Routes.h, PlotCourse). Nodes are indices into the adjacency list; what a node stands for is the
// caller's.
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

// A node's estimate of the cost from it to the goal. It has to be consistent: never more than an
// edge's cost plus the estimate at its far end, and nothing at the goal, which a straight-line
// distance is for costs that are lengths. A* then expands each node once, and the path it finds is a
// shortest one. Without one the search is Dijkstra's.
using Heuristic = std::function<double(NodeId)>;

struct Path
{
	// From the start to the goal, both included; one node when they are the same.
	std::vector<NodeId> nodes;
	double cost{0.0};
};

// A shortest path from start to goal, or nothing when the goal cannot be reached. Deterministic: the
// open nodes are expanded in order of estimated cost and then of index, and a node's way in is
// replaced only by one strictly cheaper, so of paths of equal cost the one returned is the same
// every run, and is the first the search completes. Throws std::invalid_argument for a start, a goal
// or an edge naming a node the graph does not have, and for a cost or an estimate that is negative,
// infinite or not a number.
std::optional<Path> FindPath(
    const Graph &graph, NodeId start, NodeId goal, const Heuristic &heuristic = {});
} // namespace Jettison::Ai
