#include <jettison/dock/Docks.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/mat3x3.hpp>

#include <algorithm>
#include <cmath>

#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/profile/Profile.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Sorted.h"

// Each station's deck, the docks' step 5 (docs/architecture.md, "Docks"): the buggies that carry a
// docked ship's cargo between its pad's bay and its hatch, driven along the layout's roads.
namespace Jettison::Dock
{
namespace
{
using Detail::Sorted;

constexpr glm::dvec3 kUp{0.0, 1.0, 0.0};
// How far outside a node's reach a buggy waits for it.
constexpr double kShortOf = 1.0e-6;

glm::dvec3 NodeAt(const RoadGraph &roads, const Buggy &buggy, std::uint32_t node)
{
	return roads.nodes[node].kind == RoadNodeKind::Hatch ? buggy.hatch : roads.nodes[node].position;
}

// Whether the buggy holds the node: within reach of it on the edge it is on, before or after it.
bool Holds(const RoadGraph &roads, const Buggy &buggy, std::uint32_t node)
{
	if (buggy.leg + 1 >= buggy.route.size())
		return buggy.route.back() == node;
	const std::uint32_t from = buggy.route[buggy.leg];
	const std::uint32_t to = buggy.route[buggy.leg + 1];
	const double length = glm::distance(NodeAt(roads, buggy, from), NodeAt(roads, buggy, to));
	return (to == node && length - buggy.along <= kNodeReach) ||
	       (from == node && buggy.along <= kNodeReach);
}

bool HeldByAnother(
    const entt::registry &registry, const RoadGraph &roads, const std::vector<entt::entity> &buggies,
    entt::entity self, std::uint32_t node)
{
	return std::ranges::any_of(
	    buggies,
	    [&](entt::entity other)
	    {
		    return other != self && registry.valid(other) &&
		           Holds(roads, registry.get<Buggy>(other), node);
	    });
}

// The station's buggies, in entity order.
std::vector<entt::entity> BuggiesOf(const entt::registry &registry, entt::entity station)
{
	std::vector<entt::entity> found;
	for (const entt::entity buggy : Sorted<Buggy>(registry))
		if (registry.get<Buggy>(buggy).station == station)
			found.push_back(buggy);
	return found;
}

// Whether the ship, on the pad, has cargo for another buggy: crates in the bay for it, or more in
// its hold than the buggies already on their way to fetch them will take.
bool Wanted(const entt::registry &registry, entt::entity pad, entt::entity ship, std::int64_t trip)
{
	const auto *held = registry.valid(pad) ? registry.try_get<Pad>(pad) : nullptr;
	if (!held || held->state != PadState::Holding || held->ship != ship || !registry.valid(ship))
		return false;
	const auto *transfer = registry.try_get<Transfer>(ship);
	if (!transfer)
		return false;
	if (transfer->aboard > 0)
		return true;
	std::int64_t fetching = 0;
	for (const auto [entity, buggy] : registry.view<const Buggy>().each())
		if (buggy.ship == ship && buggy.outbound && buggy.crates == 0)
			fetching += trip;
	return transfer->ashore > fetching;
}

std::int64_t Take(std::int64_t &from, std::int64_t most)
{
	const std::int64_t taken = std::min(from, most);
	from -= taken;
	return taken;
}
} // namespace

bool Carry(entt::registry &registry, entt::entity ship, std::int64_t crates)
{
	const auto *docking = registry.valid(ship) ? registry.try_get<Docking>(ship) : nullptr;
	if (!docking || docking->stage != Stage::Docked)
		return false;
	Transfer &transfer = registry.get_or_emplace<Transfer>(ship);
	if (crates > 0)
		transfer.aboard += crates - Take(transfer.ashore, crates);
	else
		transfer.ashore += -crates - Take(transfer.aboard, -crates);
	return true;
}

std::int64_t ToCarry(const entt::registry &registry, entt::entity ship)
{
	const auto *transfer = registry.valid(ship) ? registry.try_get<Transfer>(ship) : nullptr;
	if (!transfer)
		return 0;
	std::int64_t crates = transfer->aboard + transfer->ashore;
	for (const auto [entity, buggy] : registry.view<const Buggy>().each())
		if (buggy.ship == ship && buggy.outbound)
			crates += buggy.crates;
	return crates;
}

void Docks::WorkDecks(Sim::SimWorld &world, double time)
{
	JETTISON_PROFILE_ZONE("Dock::Docks::WorkDecks");
	entt::registry &registry = world.Registry();

	// A buggy whose station or pad has gone goes with it, and a transfer whose ship is no longer
	// docked is over.
	for (const entt::entity buggy : Sorted<Buggy>(registry))
	{
		const Buggy &driven = registry.get<Buggy>(buggy);
		if (!registry.valid(driven.station) || !registry.all_of<Station>(driven.station) ||
		    !registry.valid(driven.pad) || !registry.all_of<Pad>(driven.pad))
			registry.destroy(buggy);
	}
	for (const entt::entity ship : Sorted<Transfer>(registry))
	{
		const auto *docking = registry.try_get<Docking>(ship);
		if (!docking || docking->stage != Stage::Docked)
			registry.remove<Transfer>(ship);
	}

	for (const entt::entity stationEntity : Sorted<Station>(registry))
	{
		const Station &station = registry.get<Station>(stationEntity);
		const StationLayout &layout = m_layouts.at(station.layout);
		const RoadGraph &roads = m_roads.at(station.layout);
		const std::vector<entt::entity> buggies = BuggiesOf(registry, stationEntity);
		for (const entt::entity buggy : buggies)
			if (registry.valid(buggy))
				DriveBuggy(world, buggy, buggies);
		for (const entt::entity buggy : buggies)
			if (registry.valid(buggy))
				PlaceBuggy(world, buggy, time);
		SendBuggies(world, stationEntity, time);

		// A pad no road reaches has its cargo at once; a station whose buggies are parked moves it
		// at their rate.
		for (const entt::entity padEntity : registry.get<Station>(stationEntity).pads)
		{
			const Pad &pad = registry.get<Pad>(padEntity);
			if (pad.state != PadState::Holding || !registry.valid(pad.ship))
				continue;
			auto *transfer = registry.try_get<Transfer>(pad.ship);
			if (!transfer)
				continue;
			if (!roads.pads.at(pad.number - 1).reached)
				transfer->aboard = transfer->ashore = 0;
			else if (!layout.traffic.working)
				MoveParked(world, pad.ship);
		}
	}

	for (const entt::entity ship : Sorted<Transfer>(registry))
	{
		const Transfer &transfer = registry.get<Transfer>(ship);
		if (transfer.aboard > 0 || transfer.ashore > 0 || ToCarry(registry, ship) > 0)
			continue;
		const Docking &docking = registry.get<Docking>(ship);
		world.Events().Emit(
		    CargoCarried{ship, docking.station, docking.pad, registry.get<Pad>(docking.pad).number});
		registry.remove<Transfer>(ship);
	}
}

void Docks::DriveBuggy(
    Sim::SimWorld &world, entt::entity entity, const std::vector<entt::entity> &others)
{
	entt::registry &registry = world.Registry();
	Buggy &buggy = registry.get<Buggy>(entity);
	const Station &station = registry.get<Station>(buggy.station);
	const RoadGraph &roads = m_roads.at(station.layout);
	double budget = double{m_layouts.at(station.layout).traffic.speed} * world.FixedStep();
	while (budget > 0.0 && buggy.leg + 1 < buggy.route.size())
	{
		const std::uint32_t from = buggy.route[buggy.leg];
		const std::uint32_t to = buggy.route[buggy.leg + 1];
		const double length = glm::distance(NodeAt(roads, buggy, from), NodeAt(roads, buggy, to));
		// No nearer the buggy ahead in its lane, and short of the node if another holds it.
		double limit = length;
		for (const entt::entity other : others)
		{
			if (other == entity || !registry.valid(other))
				continue;
			const Buggy &ahead = registry.get<Buggy>(other);
			if (ahead.leg + 1 < ahead.route.size() && ahead.route[ahead.leg] == from &&
			    ahead.route[ahead.leg + 1] == to && ahead.along > buggy.along)
				limit = std::min(limit, ahead.along - kBuggyGap);
		}
		if (length - buggy.along > kNodeReach && HeldByAnother(registry, roads, others, entity, to))
			// Just outside the node's reach, so it is still not holding it as the next step begins.
			limit = std::min(limit, length - kNodeReach - kShortOf);
		const double advance = std::clamp(limit - buggy.along, 0.0, budget);
		buggy.along += advance;
		budget -= advance;
		if (buggy.along < length - 1.0e-9)
			return;
		++buggy.leg;
		buggy.along = 0.0;
		if (buggy.leg + 1 >= buggy.route.size())
			return ArriveBuggy(world, entity);
	}
}

void Docks::ArriveBuggy(Sim::SimWorld &world, entt::entity entity)
{
	entt::registry &registry = world.Registry();
	Buggy &buggy = registry.get<Buggy>(entity);
	const Station &station = registry.get<Station>(buggy.station);
	const std::int64_t trip = m_layouts.at(station.layout).traffic.cratesPerTrip;
	const RoadGraph &roads = m_roads.at(station.layout);
	const auto turn = [&buggy](bool outbound)
	{
		std::ranges::reverse(buggy.route);
		buggy.leg = 0;
		buggy.along = 0.0;
		buggy.outbound = outbound;
	};
	if (roads.nodes[buggy.route.back()].kind == RoadNodeKind::Hatch)
	{
		// Its crates handed aboard and a load taken off; or, its ship gone, its crates brought back.
		const Pad &pad = registry.get<Pad>(buggy.pad);
		if (pad.state == PadState::Holding && pad.ship == buggy.ship && registry.valid(buggy.ship))
			if (auto *transfer = registry.try_get<Transfer>(buggy.ship))
				buggy.crates = Take(transfer->ashore, trip);
		return turn(false);
	}
	// At the bay its load is left, and it goes out again or in.
	buggy.crates = 0;
	if (!Wanted(registry, buggy.pad, buggy.ship, trip))
	{
		registry.destroy(entity);
		return;
	}
	buggy.crates = Take(registry.get<Transfer>(buggy.ship).aboard, trip);
	buggy.hatch = HatchOnDeck(registry, buggy.ship);
	turn(true);
}

void Docks::PlaceBuggy(Sim::SimWorld &world, entt::entity entity, double time)
{
	entt::registry &registry = world.Registry();
	const Buggy &buggy = registry.get<Buggy>(entity);
	const RoadGraph &roads = m_roads.at(registry.get<Station>(buggy.station).layout);
	const glm::dvec3 from = NodeAt(roads, buggy, buggy.route[buggy.leg]);
	const glm::dvec3 to = NodeAt(roads, buggy, buggy.route[buggy.leg + 1]);
	const glm::dvec3 ahead = glm::normalize(to - from);
	const glm::dvec3 right = glm::cross(ahead, kUp);
	const glm::dvec3 at = from + ahead * buggy.along + right * kLaneOffset;
	const glm::dquat facing = glm::normalize(glm::quat_cast(glm::dmat3{right, kUp, -ahead}));
	const Physics::OnRails &rails = registry.get<Physics::OnRails>(buggy.station);
	const Physics::OnRails place{rails.body, rails.offset + rails.turn * at, rails.turn * facing};
	registry.emplace_or_replace<Sim::Transform>(
	    entity, m_physics.RailsPlace(registry.get<Sim::InSystem>(buggy.station).system, place, time));
}

void Docks::SendBuggies(Sim::SimWorld &world, entt::entity stationEntity, double time)
{
	entt::registry &registry = world.Registry();
	const Station &station = registry.get<Station>(stationEntity);
	const DeckTraffic &traffic = m_layouts.at(station.layout).traffic;
	const RoadGraph &roads = m_roads.at(station.layout);
	if (!traffic.working)
		return;
	const std::vector<entt::entity> pads = station.pads;
	for (const entt::entity padEntity : pads)
	{
		const Pad &pad = registry.get<Pad>(padEntity);
		const PadRoad &road = roads.pads.at(pad.number - 1);
		if (!road.reached || !Wanted(registry, padEntity, pad.ship, traffic.cratesPerTrip))
			continue;
		const std::vector<entt::entity> buggies = BuggiesOf(registry, stationEntity);
		const auto out = std::ranges::count_if(
		    buggies, [&](entt::entity buggy) { return registry.get<Buggy>(buggy).pad == padEntity; });
		if (out >= static_cast<std::ptrdiff_t>(traffic.buggies) ||
		    HeldByAnother(registry, roads, buggies, entt::null, road.route.front()))
			continue;
		const entt::entity ship = pad.ship;
		Buggy sent;
		sent.station = stationEntity;
		sent.pad = padEntity;
		sent.ship = ship;
		sent.route.assign(road.route.begin(), road.route.end());
		sent.hatch = HatchOnDeck(registry, ship);
		sent.crates = Take(registry.get<Transfer>(ship).aboard, traffic.cratesPerTrip);
		const entt::entity buggy = registry.create();
		// A copy: the station's is in the storage the buggy's goes into.
		const Sim::InSystem system = registry.get<Sim::InSystem>(stationEntity);
		registry.emplace<Sim::InSystem>(buggy, system);
		registry.emplace<Buggy>(buggy, std::move(sent));
		PlaceBuggy(world, buggy, time);
	}
}

void Docks::MoveParked(Sim::SimWorld &world, entt::entity ship)
{
	entt::registry &registry = world.Registry();
	const Docking &docking = registry.get<Docking>(ship);
	const Pad &pad = registry.get<Pad>(docking.pad);
	const std::string &layout = registry.get<Station>(pad.station).layout;
	const DeckTraffic &traffic = m_layouts.at(layout).traffic;
	const RoadGraph &roads = m_roads.at(layout);
	// A buggy's round trip at its speed, in whole steps, shared among the pad's buggies.
	const double trip = 2.0 *
	                    RoadLength(roads, roads.pads.at(pad.number - 1), HatchOnDeck(registry, ship)) /
	                    double{traffic.speed} / world.FixedStep();
	const auto interval = std::max<std::uint64_t>(
	    static_cast<std::uint64_t>(std::ceil(trip / static_cast<double>(traffic.buggies))), 1);
	Transfer &transfer = registry.get<Transfer>(ship);
	const std::uint64_t step = world.Clock().Step();
	// The first load moved as the interval's last step ends, counting the step it began in.
	if (transfer.next == 0)
		transfer.next = step + interval - 1;
	if (step < transfer.next)
		return;
	Take(transfer.aboard, traffic.cratesPerTrip);
	Take(transfer.ashore, traffic.cratesPerTrip);
	transfer.next += interval;
}

glm::dvec3 Docks::HatchOnDeck(const entt::registry &registry, entt::entity ship) const
{
	const Docking &docking = registry.get<Docking>(ship);
	const PadLayout &place = PadOf(registry, registry.get<Pad>(docking.pad));
	const HullDefinition &hull = *HullOf(registry, ship);
	return place.position +
	       PadOrientation(place) * glm::dvec3{double{hull.hatch.x}, 0.0, double{hull.hatch.z}};
}

double Docks::RoadLength(const RoadGraph &roads, const PadRoad &road, const glm::dvec3 &hatch) const
{
	Buggy along;
	along.hatch = hatch;
	double length = 0.0;
	for (std::size_t index = 1; index < road.route.size(); ++index)
		length += glm::distance(
		    NodeAt(roads, along, road.route[index - 1]), NodeAt(roads, along, road.route[index]));
	return length;
}
} // namespace Jettison::Dock
