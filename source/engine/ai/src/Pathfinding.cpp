#include <jettison/ai/Pathfinding.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace Jettison::Ai
{
namespace
{
bool Admissible(double cost)
{
	return std::isfinite(cost) && cost >= 0.0;
}
} // namespace

std::optional<Path> FindPath(const Graph &graph, NodeId start, NodeId goal, const Heuristic &heuristic)
{
	const std::size_t count = graph.adjacency.size();
	if (start >= count || goal >= count)
		throw std::invalid_argument{std::format(
		    "Ai: a path from node {} to node {} in a graph of {} nodes", start, goal, count)};
	for (std::size_t node = 0; node < count; ++node)
		for (const Edge &edge : graph.adjacency[node])
		{
			if (edge.to >= count)
				throw std::invalid_argument{std::format(
				    "Ai: node {} has an edge to node {}, in a graph of {} nodes", node, edge.to,
				    count)};
			if (!Admissible(edge.cost))
				throw std::invalid_argument{std::format(
				    "Ai: the edge from node {} to node {} costs {}, which is not a cost", node, edge.to,
				    edge.cost)};
		}

	constexpr double kUnknown = -1.0;
	std::vector<double> estimates(count, kUnknown);
	const auto estimate = [&](NodeId node)
	{
		if (estimates[node] == kUnknown)
		{
			const double value = heuristic ? heuristic(node) : 0.0;
			if (!Admissible(value))
				throw std::invalid_argument{std::format(
				    "Ai: the estimate from node {} to the goal is {}, which is not a cost", node,
				    value)};
			estimates[node] = value;
		}
		return estimates[node];
	};

	constexpr NodeId kNone = std::numeric_limits<NodeId>::max();
	std::vector<double> cost(count, std::numeric_limits<double>::infinity());
	std::vector<NodeId> previous(count, kNone);
	std::vector<bool> closed(count, false);
	// Ordered by estimated cost, then by node: the pair's own order, so a tie is the lower index.
	using Open = std::pair<double, NodeId>;
	std::priority_queue<Open, std::vector<Open>, std::greater<>> open;
	cost[start] = 0.0;
	open.emplace(estimate(start), start);
	while (!open.empty())
	{
		const NodeId node = open.top().second;
		open.pop();
		// A node is pushed again each time a cheaper way in is found; the entries it left are stale.
		if (closed[node])
			continue;
		closed[node] = true;
		if (node == goal)
		{
			Path path{{}, cost[goal]};
			for (NodeId at = goal; at != kNone; at = previous[at])
				path.nodes.push_back(at);
			std::ranges::reverse(path.nodes);
			return path;
		}
		for (const Edge &edge : graph.adjacency[node])
		{
			if (closed[edge.to])
				continue;
			const double through = cost[node] + edge.cost;
			if (through < cost[edge.to])
			{
				cost[edge.to] = through;
				previous[edge.to] = node;
				open.emplace(through + estimate(edge.to), edge.to);
			}
		}
	}
	return std::nullopt;
}
} // namespace Jettison::Ai
