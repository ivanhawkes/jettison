#include <jettison/ai/Pathfinding.h>
#include <jettison/math/Random.h>

#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace Jettison;
using Ai::Edge;
using Ai::Graph;
using Ai::NodeId;

namespace
{
void Join(Graph &graph, NodeId a, NodeId b, double cost)
{
	graph.adjacency[a].push_back({b, cost});
	graph.adjacency[b].push_back({a, cost});
}

// Every pair's least cost, by Floyd and Warshall: what the search is checked against, by a method
// that shares nothing with it.
std::vector<std::vector<double>> AllPairs(const Graph &graph)
{
	const std::size_t count = graph.adjacency.size();
	constexpr double kNever = std::numeric_limits<double>::infinity();
	std::vector<std::vector<double>> cost(count, std::vector<double>(count, kNever));
	for (std::size_t node = 0; node < count; ++node)
	{
		cost[node][node] = 0.0;
		for (const Edge &edge : graph.adjacency[node])
			cost[node][edge.to] = std::min(cost[node][edge.to], edge.cost);
	}
	for (std::size_t via = 0; via < count; ++via)
		for (std::size_t from = 0; from < count; ++from)
			for (std::size_t to = 0; to < count; ++to)
				cost[from][to] = std::min(cost[from][to], cost[from][via] + cost[via][to]);
	return cost;
}

// The cost of walking the path's nodes along the graph's cheapest edges between them, or infinity
// for a step no edge makes.
double Walk(const Graph &graph, const std::vector<NodeId> &nodes)
{
	double total = 0.0;
	for (std::size_t step = 1; step < nodes.size(); ++step)
	{
		double cheapest = std::numeric_limits<double>::infinity();
		for (const Edge &edge : graph.adjacency[nodes[step - 1]])
			if (edge.to == nodes[step])
				cheapest = std::min(cheapest, edge.cost);
		total += cheapest;
	}
	return total;
}

std::size_t Cell(int width, int x, int y)
{
	return static_cast<std::size_t>(x) +
	       (static_cast<std::size_t>(width) * static_cast<std::size_t>(y));
}

// A grid of cells a unit apart, each joined to its four neighbours unless one is a wall, with node
// x + width * y.
Graph Grid(int width, int height, const std::vector<bool> &walls)
{
	Graph graph;
	graph.adjacency.resize(Cell(width, 0, height));
	const auto open = [&](int x, int y) { return !walls[Cell(width, x, y)]; };
	for (int y = 0; y < height; ++y)
		for (int x = 0; x < width; ++x)
		{
			if (!open(x, y))
				continue;
			const auto node = static_cast<NodeId>(Cell(width, x, y));
			if (x + 1 < width && open(x + 1, y))
				Join(graph, node, node + 1, 1.0);
			if (y + 1 < height && open(x, y + 1))
				Join(graph, node, node + static_cast<NodeId>(width), 1.0);
		}
	return graph;
}
} // namespace

TEST_CASE("Pathfinding: a small graph's shortest path, by its known answer")
{
	// The cheapest way from 0 to 4 is 0-2-1-3-4, at 1 + 2 + 1 + 3: going straight to 1 costs 7,
	// and 2-3 costs 8.
	Graph graph;
	graph.adjacency.resize(5);
	Join(graph, 0, 1, 7.0);
	Join(graph, 0, 2, 1.0);
	Join(graph, 1, 2, 2.0);
	Join(graph, 1, 3, 1.0);
	Join(graph, 2, 3, 8.0);
	Join(graph, 3, 4, 3.0);

	const auto path = Ai::FindPath(graph, 0, 4);
	REQUIRE(path);
	CHECK(path->nodes == std::vector<NodeId>{0, 2, 1, 3, 4});
	CHECK(path->cost == 7.0);

	const auto still = Ai::FindPath(graph, 3, 3);
	REQUIRE(still);
	CHECK(still->nodes == std::vector<NodeId>{3});
	CHECK(still->cost == 0.0);
}

TEST_CASE("Pathfinding: edges are one way, and a goal they do not reach is no path")
{
	Graph graph;
	graph.adjacency.resize(4);
	graph.adjacency[0].push_back({1, 1.0});
	graph.adjacency[1].push_back({2, 1.0});
	CHECK(Ai::FindPath(graph, 0, 2));
	CHECK_FALSE(Ai::FindPath(graph, 2, 0));
	CHECK_FALSE(Ai::FindPath(graph, 0, 3));
}

TEST_CASE("Pathfinding: of paths that cost the same, the same one every time")
{
	// 0-1-3 and 0-2-3 both cost 2. Node 1 is expanded before 2, so 3 is reached through it first,
	// and 2 does not replace it, being no cheaper. The adjacency's order does not change that.
	for (const bool reversed : {false, true})
	{
		Graph graph;
		graph.adjacency.resize(4);
		if (reversed)
		{
			Join(graph, 0, 2, 1.0);
			Join(graph, 0, 1, 1.0);
		}
		else
		{
			Join(graph, 0, 1, 1.0);
			Join(graph, 0, 2, 1.0);
		}
		Join(graph, 2, 3, 1.0);
		Join(graph, 1, 3, 1.0);
		const auto path = Ai::FindPath(graph, 0, 3);
		REQUIRE(path);
		CHECK(path->nodes == std::vector<NodeId>{0, 1, 3});
	}
}

TEST_CASE("Pathfinding: a grid round a wall, with and without an estimate, against its known length")
{
	// A 9 x 7 grid with a wall down x = 4 from y = 0 to y = 5, so the way from (0, 0) to (8, 0)
	// goes up past the wall's end and back: 8 across and 6 up and 6 down, 20 steps.
	constexpr int kWidth = 9;
	constexpr int kHeight = 7;
	std::vector<bool> walls(Cell(kWidth, 0, kHeight), false);
	for (int y = 0; y <= 5; ++y)
		walls[Cell(kWidth, 4, y)] = true;
	const Graph graph = Grid(kWidth, kHeight, walls);
	constexpr NodeId kStart = 0;
	constexpr NodeId kGoal = 8;
	// The Manhattan distance is consistent on a grid of unit steps.
	const auto manhattan = [](NodeId node)
	{
		const int x = static_cast<int>(node) % kWidth;
		const int y = static_cast<int>(node) / kWidth;
		return static_cast<double>(std::abs(x - 8) + std::abs(y - 0));
	};

	const auto informed = Ai::FindPath(graph, kStart, kGoal, manhattan);
	const auto blind = Ai::FindPath(graph, kStart, kGoal);
	REQUIRE(informed);
	REQUIRE(blind);
	CHECK(informed->cost == 20.0);
	CHECK(blind->cost == 20.0);
	CHECK(informed->nodes.size() == 21);
	CHECK(Walk(graph, informed->nodes) == 20.0);

	// Walled off entirely, there is none.
	walls[Cell(kWidth, 4, 6)] = true;
	CHECK_FALSE(Ai::FindPath(Grid(kWidth, kHeight, walls), kStart, kGoal, manhattan));
}

TEST_CASE("Pathfinding: random graphs, against every pair's least cost, the same every run")
{
	Math::Random random{9001};
	for (int trial = 0; trial < 20; ++trial)
	{
		const int count = random.Range(2, 30);
		Graph graph;
		graph.adjacency.resize(static_cast<std::size_t>(count));
		const int edges = random.Range(0, count * 4);
		for (int edge = 0; edge < edges; ++edge)
		{
			const auto from = static_cast<NodeId>(random.Range(0, count - 1));
			const auto to = static_cast<NodeId>(random.Range(0, count - 1));
			// Whole costs, and some of none, so ties are common and sums exact.
			graph.adjacency[from].push_back({to, static_cast<double>(random.Range(0, 9))});
		}
		const std::vector<std::vector<double>> least = AllPairs(graph);
		for (NodeId from = 0; from < static_cast<NodeId>(count); ++from)
			for (NodeId to = 0; to < static_cast<NodeId>(count); ++to)
			{
				const auto path = Ai::FindPath(graph, from, to);
				CAPTURE(trial);
				CAPTURE(from);
				CAPTURE(to);
				if (std::isinf(least[from][to]))
				{
					CHECK_FALSE(path);
					continue;
				}
				REQUIRE(path);
				CHECK(path->cost == least[from][to]);
				CHECK(path->nodes.front() == from);
				CHECK(path->nodes.back() == to);
				CHECK(Walk(graph, path->nodes) == path->cost);
				const auto again = Ai::FindPath(graph, from, to);
				REQUIRE(again);
				CHECK(again->nodes == path->nodes);
			}
	}
}

TEST_CASE("Pathfinding: a graph or an estimate that is not one is refused")
{
	Graph graph;
	graph.adjacency.resize(2);
	graph.adjacency[0].push_back({1, 1.0});
	CHECK_THROWS_AS(Ai::FindPath(graph, 0, 2), std::invalid_argument);
	CHECK_THROWS_AS(Ai::FindPath(graph, 2, 0), std::invalid_argument);
	CHECK_THROWS_AS(Ai::FindPath(graph, 0, 1, [](NodeId) { return -1.0; }), std::invalid_argument);

	for (const double cost :
	     {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
	{
		Graph bad = graph;
		bad.adjacency[1].push_back({0, cost});
		CHECK_THROWS_AS(Ai::FindPath(bad, 0, 1), std::invalid_argument);
	}
	Graph dangling = graph;
	dangling.adjacency[1].push_back({5, 1.0});
	CHECK_THROWS_AS(Ai::FindPath(dangling, 0, 1), std::invalid_argument);
}
