#pragma once

#include <glm/vec3.hpp>

#include <cstdint>
#include <utility>
#include <vector>

#include <jettison/ai/Pathfinding.h>
#include <jettison/dock/Definitions.h>

namespace Jettison::Dock
{
// A station's roads (9e): the graph its buggies drive on between its cargo bays and its pads'
// hatches, made from its layout, so it is the layout's and never state. Roads run on the decks'
// and piers' tops, the faces that look along the station's +Y, at the height of what they join:
//
//   - Each pier's road runs its length, down the wider strip of its top beside its pads (the side
//     towards +X or +Z of two as wide), when that strip holds a road, kRoadWidth; along the middle
//     of a pier with no pads on it. Its ends are half a road in from the pier's.
//   - Each pad not for display has an apron, the middle of its aft edge, where a buggy comes onto
//     it, and its hatch, on its centre line HatchAft behind its middle; a road straight between the
//     two, on the pad.
//   - Each bay and each apron is joined to every road at the road's nearest point to it, and each bay
//     to each apron, by a straight leg wherever the leg lies on the deck, passes through nothing of
//     the station and crosses no pad.
//
// Points where legs join a road are its nodes, in order along it, and one nearer the last than
// twice kNodeReach is that one. Every edge is a straight road both ways, costing its length. A
// buggy holds the node it is within kNodeReach of, and the next waits short of it, so a node is a
// junction one buggy crosses at a time; an edge shorter than twice that is not made.
enum class RoadNodeKind : std::uint8_t
{
	Bay,
	Apron,
	Hatch,
	Road
};

struct RoadNode
{
	// In the station's axes, on the deck.
	glm::dvec3 position{0.0};
	RoadNodeKind kind{RoadNodeKind::Road};
	// The bay's index, or the pad's (its number less one); a road's index for a road node.
	std::uint32_t index{0};
};

// A pad's way to its bay: the bay nearest it by road, the lowest numbered of two as near, and the
// nodes from that bay's to the pad's hatch, by A* (Ai::FindPath) with the straight-line distance.
// Unreached for a display pad, and for a pad no bay reaches.
struct PadRoad
{
	bool reached{false};
	std::uint32_t apron{0};
	std::uint32_t hatch{0};
	std::uint32_t bay{0};
	std::vector<Ai::NodeId> route;
	double length{0.0};
};

struct RoadGraph
{
	std::vector<RoadNode> nodes;
	Ai::Graph graph;
	// By the pad's number less one.
	std::vector<PadRoad> pads;
	// Each bay's node, by the bay's index.
	std::vector<Ai::NodeId> bays;
	// The edges once each, from the lower node to the higher, for drawing.
	std::vector<std::pair<Ai::NodeId, Ai::NodeId>> edges;
};

// A road two buggies wide: a lane each way, a buggy on the right of the way it goes.
inline constexpr double kRoadWidth = 8.0;
inline constexpr double kLaneOffset = 2.0;
// How near a node a buggy holds it, before it and after it.
inline constexpr double kNodeReach = 6.0;
// How near the buggy ahead in its lane a buggy comes, middle to middle: 4 m buggies a length apart.
inline constexpr double kBuggyGap = 8.0;

// The layout's roads. A layout with no bays has none, and every pad is unreached.
RoadGraph Roads(const StationLayout &layout);
} // namespace Jettison::Dock
