#include <jettison/dock/Roads.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace Jettison::Dock
{
namespace
{
// How near two heights are to be one deck, and how far a leg may run along an edge it does not
// cross: a road beside a pad's edge, or a bay's sill on the wall's foot.
constexpr double kTolerance = 0.01;
// The shortest edge, so a buggy holding a node never holds the next.
constexpr double kShortest = 2.0 * kNodeReach;

glm::dvec2 Flat(const glm::dvec3 &point)
{
	return {point.x, point.z};
}

bool Up(const PadLayout &pad)
{
	return pad.normal.y > 1.0 - 1.0e-6;
}

double Top(const Box &box)
{
	return box.centre.y + box.size.y * 0.5;
}

// The part of the segment from a to b, as a fraction of it, within the rectangle, or none: the
// slab test.
std::optional<std::pair<double, double>> Clip(
    const glm::dvec2 &a, const glm::dvec2 &b, const glm::dvec2 &low, const glm::dvec2 &high)
{
	double enter = 0.0;
	double leave = 1.0;
	const glm::dvec2 way = b - a;
	for (glm::length_t axis = 0; axis < 2; ++axis)
	{
		if (std::abs(way[axis]) < 1.0e-12)
		{
			if (a[axis] < low[axis] || a[axis] > high[axis])
				return std::nullopt;
			continue;
		}
		double near = (low[axis] - a[axis]) / way[axis];
		double far = (high[axis] - a[axis]) / way[axis];
		if (near > far)
			std::swap(near, far);
		enter = std::max(enter, near);
		leave = std::min(leave, far);
		if (enter > leave)
			return std::nullopt;
	}
	return std::pair{enter, leave};
}

std::vector<Box> Structure(const StationLayout &layout)
{
	std::vector<Box> boxes = layout.decks;
	boxes.insert(boxes.end(), layout.piers.begin(), layout.piers.end());
	return boxes;
}

// The pad's axes on the deck: across it, and back along its heading.
std::pair<glm::dvec3, glm::dvec3> PadAxes(const PadLayout &pad)
{
	const glm::dvec3 back = -pad.heading;
	return {glm::cross(pad.normal, back), back};
}

// Whether a straight leg from a to b, both at the deck's height, lies on the tops of the
// structure's boxes there the whole way, through none of them, and across no pad.
bool Clear(const StationLayout &layout, const glm::dvec3 &a, const glm::dvec3 &b)
{
	const double height = a.y;
	const double length = glm::length(Flat(b) - Flat(a));
	if (std::abs(b.y - height) > kTolerance || length < kTolerance)
		return false;
	const double slack = kTolerance / length;
	std::vector<std::pair<double, double>> on;
	for (const Box &box : Structure(layout))
	{
		const glm::dvec2 low = Flat(box.centre - box.size * 0.5);
		const glm::dvec2 high = Flat(box.centre + box.size * 0.5);
		const double bottom = box.centre.y - box.size.y * 0.5;
		if (std::abs(Top(box) - height) <= kTolerance)
		{
			if (const auto part =
			        Clip(Flat(a), Flat(b), low - glm::dvec2{kTolerance}, high + glm::dvec2{kTolerance}))
				on.push_back(*part);
		}
		else if (bottom < height - kTolerance && Top(box) > height + kTolerance)
			if (const auto part =
			        Clip(Flat(a), Flat(b), low + glm::dvec2{kTolerance}, high - glm::dvec2{kTolerance});
			    part && part->second - part->first > slack)
				return false;
	}
	std::ranges::sort(on);
	double covered = 0.0;
	for (const auto &[from, to] : on)
	{
		if (from > covered + slack)
			return false;
		covered = std::max(covered, to);
	}
	if (covered < 1.0 - slack)
		return false;

	for (const PadLayout &pad : layout.pads)
	{
		if (!Up(pad) || std::abs(pad.position.y - height) > kTolerance)
			continue;
		const auto [across, back] = PadAxes(pad);
		const auto local = [&](const glm::dvec3 &point)
		{
			const glm::dvec3 offset = point - pad.position;
			return glm::dvec2{glm::dot(offset, across), glm::dot(offset, back)};
		};
		const glm::dvec2 half = glm::dvec2{PadSize(pad.padClass)} * 0.5 - kTolerance;
		if (const auto part = Clip(local(a), local(b), -half, half);
		    part && part->second - part->first > slack)
			return false;
	}
	return true;
}

// A pier's road, from its start to its end along the pier, and the legs that join it: how far along
// it each joins, and the node it joins from.
struct Road
{
	glm::dvec3 start{0.0};
	glm::dvec3 end{0.0};
	std::vector<std::pair<double, Ai::NodeId>> joins;
};

std::optional<Road> PierRoad(const StationLayout &layout, const Box &pier)
{
	const bool alongX = pier.size.x >= pier.size.z;
	const glm::dvec3 along = alongX ? glm::dvec3{1.0, 0.0, 0.0} : glm::dvec3{0.0, 0.0, 1.0};
	const glm::dvec3 side = alongX ? glm::dvec3{0.0, 0.0, 1.0} : glm::dvec3{1.0, 0.0, 0.0};
	const double length = glm::dot(pier.size, along);
	const double width = glm::dot(pier.size, side);
	const double middle = glm::dot(pier.centre, side);
	const double top = Top(pier);

	double low = 0.0;
	double high = 0.0;
	bool any = false;
	for (const PadLayout &pad : layout.pads)
	{
		const glm::dvec3 offset = glm::abs(pad.position - pier.centre);
		if (!Up(pad) || std::abs(pad.position.y - top) > kTolerance || offset.x > pier.size.x * 0.5 ||
		    offset.z > pier.size.z * 0.5)
			continue;
		const auto [across, back] = PadAxes(pad);
		const glm::dvec2 size = PadSize(pad.padClass);
		const double reach = std::abs(glm::dot(across, side)) * size.x * 0.5 +
		                     std::abs(glm::dot(back, side)) * size.y * 0.5;
		const double at = glm::dot(pad.position, side);
		low = any ? std::min(low, at - reach) : at - reach;
		high = any ? std::max(high, at + reach) : at + reach;
		any = true;
	}
	double lateral = middle;
	if (any)
	{
		const double below = low - (middle - width * 0.5);
		const double above = middle + width * 0.5 - high;
		if (std::max(below, above) < kRoadWidth)
			return std::nullopt;
		lateral =
		    above >= below ? (high + middle + width * 0.5) * 0.5 : (middle - width * 0.5 + low) * 0.5;
	}
	const glm::dvec3 onTop =
	    along * glm::dot(pier.centre, along) + side * lateral + glm::dvec3{0.0, top, 0.0};
	const double reach = length * 0.5 - kRoadWidth * 0.5;
	if (reach <= 0.0)
		return std::nullopt;
	return Road{onTop - along * reach, onTop + along * reach, {}};
}

double Distance(const RoadGraph &roads, Ai::NodeId from, Ai::NodeId to)
{
	return glm::distance(roads.nodes[from].position, roads.nodes[to].position);
}

void Join(RoadGraph &roads, Ai::NodeId from, Ai::NodeId to)
{
	const double cost = Distance(roads, from, to);
	roads.graph.adjacency[from].push_back({to, cost});
	roads.graph.adjacency[to].push_back({from, cost});
	roads.edges.emplace_back(std::min(from, to), std::max(from, to));
}

Ai::NodeId Add(RoadGraph &roads, const glm::dvec3 &position, RoadNodeKind kind, std::size_t index)
{
	roads.nodes.push_back({position, kind, static_cast<std::uint32_t>(index)});
	roads.graph.adjacency.emplace_back();
	return static_cast<Ai::NodeId>(roads.nodes.size() - 1);
}
} // namespace

float HatchAft(PadClass padClass)
{
	return LargestHull(padClass).x * 0.21f;
}

RoadGraph Roads(const StationLayout &layout)
{
	RoadGraph roads;
	roads.pads.resize(layout.pads.size());
	if (layout.bays.empty())
		return roads;

	for (std::size_t index = 0; index < layout.bays.size(); ++index)
		roads.bays.push_back(Add(roads, layout.bays[index].position, RoadNodeKind::Bay, index));
	std::vector<Ai::NodeId> aprons;
	for (std::size_t index = 0; index < layout.pads.size(); ++index)
	{
		const PadLayout &pad = layout.pads[index];
		if (pad.display || !Up(pad))
			continue;
		const glm::dvec3 back = -pad.heading;
		PadRoad &road = roads.pads[index];
		road.apron =
		    Add(roads, pad.position + back * (double{PadSize(pad.padClass).y} * 0.5),
		        RoadNodeKind::Apron, index);
		road.hatch = Add(
		    roads, pad.position + back * double{HatchAft(pad.padClass)}, RoadNodeKind::Hatch, index);
		Join(roads, road.apron, road.hatch);
		aprons.push_back(road.apron);
	}

	// The legs that join each bay and apron to a pier's road, and each bay to each apron.
	std::vector<Road> piers;
	for (const Box &pier : layout.piers)
		if (std::optional<Road> road = PierRoad(layout, pier))
			piers.push_back(std::move(*road));
	std::vector<Ai::NodeId> joiners = roads.bays;
	joiners.insert(joiners.end(), aprons.begin(), aprons.end());
	for (const Ai::NodeId joiner : joiners)
	{
		const glm::dvec3 &from = roads.nodes[joiner].position;
		for (Road &road : piers)
		{
			const glm::dvec3 way = road.end - road.start;
			const double length = glm::length(way);
			const double along = std::clamp(glm::dot(from - road.start, way / length), 0.0, length);
			const glm::dvec3 to = road.start + way * (along / length);
			if (glm::distance(from, to) >= kShortest && Clear(layout, from, to))
				road.joins.emplace_back(along, joiner);
		}
	}
	for (const Ai::NodeId bay : roads.bays)
		for (const Ai::NodeId apron : aprons)
			if (Distance(roads, bay, apron) >= kShortest &&
			    Clear(layout, roads.nodes[bay].position, roads.nodes[apron].position))
				Join(roads, bay, apron);

	// Each road's nodes where legs join it, in order along it, one too near the last being that one.
	for (std::size_t index = 0; index < piers.size(); ++index)
	{
		Road &road = piers[index];
		std::ranges::sort(road.joins);
		const glm::dvec3 way = glm::normalize(road.end - road.start);
		std::optional<std::pair<double, Ai::NodeId>> last;
		for (const auto &[along, joiner] : road.joins)
		{
			if (!last || along - last->first >= kShortest)
			{
				const Ai::NodeId node = Add(roads, road.start + way * along, RoadNodeKind::Road, index);
				if (last)
					Join(roads, last->second, node);
				last = std::pair{along, node};
			}
			const glm::dvec3 &at = roads.nodes[last->second].position;
			if (Distance(roads, joiner, last->second) >= kShortest &&
			    Clear(layout, roads.nodes[joiner].position, at))
				Join(roads, joiner, last->second);
		}
	}

	// Each pad's way from the nearest bay to its hatch.
	for (std::size_t index = 0; index < layout.pads.size(); ++index)
	{
		PadRoad &road = roads.pads[index];
		if (layout.pads[index].display || !Up(layout.pads[index]))
			continue;
		const Ai::NodeId hatch = road.hatch;
		const Ai::Heuristic estimate = [&roads, hatch](Ai::NodeId node)
		{ return Distance(roads, node, hatch); };
		for (std::size_t bay = 0; bay < roads.bays.size(); ++bay)
		{
			const std::optional<Ai::Path> path =
			    Ai::FindPath(roads.graph, roads.bays[bay], hatch, estimate);
			if (!path || (road.reached && path->cost >= road.length))
				continue;
			road.reached = true;
			road.bay = static_cast<std::uint32_t>(bay);
			road.route = path->nodes;
			road.length = path->cost;
		}
	}
	return roads;
}
} // namespace Jettison::Dock
