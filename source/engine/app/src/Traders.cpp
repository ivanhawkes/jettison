#include "Traders.h"

#include "Stations.h"
#include "Trading.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Cruise.h>
#include <jettison/ai/Gates.h>
#include <jettison/ai/Lanes.h>
#include <jettison/ai/Routes.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/Types.h>
#include <jettison/ai/UtilityAi.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Log.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/reflect/Reflect.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

namespace Jettison::App
{
namespace
{
using Ai::Status;
using Ai::TaskContext;

constexpr Core::Category kTraderLog{"traders"};

// How far out along a gate's axis a trader lines up before it flies through the ring, and how far
// beyond the ring it aims: the gate sends it as it passes, and it never gets there.
constexpr double kRingApproach = 400.0;
// How far a trader holds off a station's sphere while it waits its turn for a pad, and how far apart
// along the line across it each place in the queue waits.
constexpr double kHoldingOff = 400.0;
constexpr double kHoldingApart = 150.0;
// What a trader allows for a stop at a station in its reckoning of a run: the approach round the
// station and onto the pad, and the cargo across the deck.
constexpr double kStopSeconds = 240.0;
// What a trader allows for a crossing of a system besides its drive's time in that reckoning: out
// from a station's pad beyond the reach inside which a ship does not cruise (2.3 km at its
// thrusters' 150 m/s, round the station), and in from a gate's arrival, 4.6 km off, to its ring.
constexpr double kCruiseHandlingSeconds = 45.0;
// A course off a station keeps this far from its sphere.
constexpr double kStationClearance = 100.0;
// How far past a station's reach a trader flies before it cruises.
constexpr double kReachMargin = 300.0;

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		Ai::RegisterTypes();
		RegisterType<Trader>("Shell.Trader")
		    .Field<&Trader::good>("good", Attribute::Saved)
		    .Field<&Trader::buyAt>("buyAt", Attribute::Saved)
		    .Field<&Trader::sellAt>("sellAt", Attribute::Saved)
		    .Field<&Trader::bound>("bound", Attribute::Saved)
		    .Field<&Trader::jumps>("jumps", Attribute::Saved)
		    .Field<&Trader::paid>("paid", Attribute::Saved)
		    .Field<&Trader::runs>("runs", Attribute::Saved)
		    .Field<&Trader::earned>("earned", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

const MarketGood *FindGood(const Market &market, std::string_view good)
{
	const auto found = std::ranges::find(market.goods, good, &MarketGood::good);
	return found != market.goods.end() ? &*found : nullptr;
}

// Every market at a station in the world, in entity order, so a choice between equals is the same
// however the storage happens to be laid out, as it is not after a load.
std::vector<entt::entity> Markets(const entt::registry &registry)
{
	std::vector<entt::entity> markets;
	for (const auto [entity, market] : registry.view<const Market>().each())
		if (market.stationEntity != entt::null)
			markets.push_back(entity);
	std::ranges::sort(markets, {}, [](entt::entity entity) { return entt::to_integral(entity); });
	return markets;
}

// What the traders' tasks share: the world's physics and lanes, the systems and the decision.
class Round
{
  public:
	Round(
	    const Physics::PhysicsWorld &physics, const Ai::LaneTravel &lanes,
	    std::vector<Celestial::StarSystem> systems, Ai::DecisionDefinition decision,
	    Script::ScriptHost *pScripts)
	    : m_physics{physics}, m_lanes{lanes}, m_systems{std::move(systems)},
	      m_decision{std::move(decision)}, m_pScripts{pScripts}
	{
	}

	void Define(Ai::Behaviours &behaviours, const std::shared_ptr<Round> &self) const
	{
		const auto task = [&](std::string name, Status (Round::*method)(TaskContext &) const)
		{
			behaviours.DefineTask(
			    std::move(name),
			    [self, method](TaskContext &context) { return ((*self).*method)(context); });
		};
		task("choose-trade", &Round::ChooseTrade);
		task("buying", &Round::Buying);
		task("head-for", &Round::HeadFor);
		task("leave", &Round::Leave);
		task("jumps-left", &Round::JumpsLeft);
		task("cruise", &Round::CruiseTo);
		task("clear", &Round::ClearGate);
		task("fly-through", &Round::FlyThrough);
		task("jump", &Round::Jump);
		task("dock", &Round::RequestDock);
		task("trade", &Round::MakeTrade);
		task("wait-cargo", &Round::WaitCargo);
	}

  private:
	// Chooses the run: with an empty hold, a good to buy at one market and sell at another; with a
	// good in the hold, where to sell it. Fails, choosing nothing, when no run beats waiting.
	Status ChooseTrade(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		// A ship on a lane is in no system to set out from.
		if (!m_lanes.Galaxy() || registry.get<Sim::InSystem>(context.entity).system == Sim::kNoSystem)
			return Status::Failure;
		Trader &trader = registry.get<Trader>(context.entity);
		const App::Hold &hold = registry.get<App::Hold>(context.entity);
		const std::int64_t credits = CreditsOf(registry, context.entity);
		const std::vector<entt::entity> markets = Markets(registry);

		struct Run
		{
			std::string_view good;
			entt::entity buyAt{entt::null};
			entt::entity sellAt{entt::null};
			std::int64_t crates{0};
			std::int64_t ask{0};
			std::int64_t bid{0};
		};
		std::vector<Run> runs;
		if (hold.cargo.empty())
		{
			for (const entt::entity buyAt : markets)
				for (const MarketGood &good : registry.get<Market>(buyAt).goods)
				{
					const std::int64_t crates = std::min(
					    {hold.capacity - hold.Used(), good.stock,
					     credits / std::max<std::int64_t>(good.ask, 1)});
					if (crates <= 0)
						continue;
					for (const entt::entity sellAt : markets)
						if (const MarketGood *sold = FindGood(registry.get<Market>(sellAt), good.good);
						    sellAt != buyAt && sold)
							runs.push_back({good.good, buyAt, sellAt, crates, good.ask, sold->bid});
				}
		}
		else
		{
			const Cargo &held = hold.cargo.front();
			for (const entt::entity sellAt : markets)
				if (const MarketGood *sold = FindGood(registry.get<Market>(sellAt), held.good))
					runs.push_back({held.good, entt::null, sellAt, held.crates, 0, sold->bid});
		}

		// The run's time and tolls: from where the ship is to the market it buys at, and on to where it
		// sells, over the lanes and across each system by course, with a stop at each; worked out only
		// for a run still in the reckoning, and once for each pair of places. A leg no route makes
		// takes no time.
		struct Leg
		{
			double seconds{-1.0};
			std::int64_t tolls{0};
		};
		std::map<std::pair<std::string, std::string>, Leg> legs;
		std::optional<Ai::CrossingCost> crossing;
		const Ai::Place here = PlaceOf(registry, context.entity);
		const auto leg = [&](const Ai::Place &from, const Ai::Place &to)
		{
			const auto key = std::pair{
			    std::format("{}/{}", from.system, from.body), std::format("{}/{}", to.system, to.body)};
			if (const auto known = legs.find(key); known != legs.end())
				return known->second;
			if (!crossing)
				crossing = Crossing(context.world, context.entity);
			const std::optional<Ai::Route> route =
			    Ai::PlotRoute(*m_lanes.Galaxy(), from, to, *crossing);
			const Leg found = route ? Leg{route->seconds + kStopSeconds, Tolls(*route)} : Leg{};
			legs.emplace(key, found);
			return found;
		};
		const auto rivals = [&](const Run &run)
		{
			double count = 0.0;
			for (const auto [entity, other] : registry.view<const Trader>().each())
				// After the same stock, or the same buyers.
				if (entity != context.entity && other.good == run.good &&
				    (run.buyAt != entt::null ? other.buyAt == run.buyAt : other.sellAt == run.sellAt))
					count += 1.0;
			return count;
		};

		std::vector<Ai::Option> options;
		options.reserve(runs.size() + 1);
		for (const Run &run : runs)
		{
			const auto inputs = [&, run](std::string_view input) -> double
			{
				if (input == "margin")
					return run.buyAt == entt::null
					           ? 1.0
					           : static_cast<double>(run.bid - run.ask) / static_cast<double>(run.ask);
				if (input == "rivals")
					return rivals(run);
				if (input == "rate")
				{
					const Ai::Place sell = MarketPlace(registry, run.sellAt);
					Leg whole;
					if (run.buyAt != entt::null)
					{
						const Ai::Place buy = MarketPlace(registry, run.buyAt);
						const Leg there = leg(here, buy);
						const Leg on = leg(buy, sell);
						if (there.seconds < 0.0 || on.seconds < 0.0)
							return 0.0;
						whole = {there.seconds + on.seconds, there.tolls + on.tolls};
					}
					else
					{
						whole = leg(here, sell);
						if (whole.seconds < 0.0)
							return 0.0;
					}
					// What the run clears, the lanes' tolls paid.
					const std::int64_t gain = run.crates * (run.bid - run.ask) - whole.tolls;
					return static_cast<double>(gain) / std::max(whole.seconds, 1.0);
				}
				throw std::invalid_argument{std::format(
				    "The trade decision asks for '{}', which a trader does not know", input)};
			};
			// A consideration may name an input a mod's script answers, from the trader's own.
			options.push_back(
			    {run.buyAt == entt::null ? "sell" : "trade",
			     m_pScripts ? m_pScripts->WithScripts(inputs) : Ai::Inputs{inputs}});
		}
		const Ai::Inputs waiting = [](std::string_view) { return 0.0; };
		options.push_back({"wait", m_pScripts ? m_pScripts->WithScripts(waiting) : waiting});

		const std::optional<Ai::Choice> choice = Ai::Choose(m_decision, options);
		if (!choice || choice->option == runs.size())
		{
			trader.good.clear();
			trader.buyAt = entt::null;
			trader.sellAt = entt::null;
			return Status::Failure;
		}
		const Run &run = runs[choice->option];
		trader.good = std::string{run.good};
		trader.buyAt = run.buyAt;
		trader.sellAt = run.sellAt;
		trader.paid = 0;
		return Status::Success;
	}

	// Whether the run buys, rather than selling what the hold already has.
	Status Buying(TaskContext &context) const
	{
		return context.world.Registry().get<Trader>(context.entity).buyAt != entt::null
		           ? Status::Success
		           : Status::Failure;
	}

	// Makes for the run's market (the argument says which, buy or sell): the route from the ship's
	// system to the market's, as the gates it goes through.
	Status HeadFor(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		Trader &trader = registry.get<Trader>(context.entity);
		trader.bound =
		    !context.arguments.empty() && context.arguments[0] == "buy" ? trader.buyAt : trader.sellAt;
		trader.jumps.clear();
		if (trader.bound == entt::null || !m_lanes.Galaxy() ||
		    registry.get<Sim::InSystem>(context.entity).system == Sim::kNoSystem)
			return Status::Failure;
		const Ai::Place to = MarketPlace(registry, trader.bound);
		const std::optional<Ai::Route> route = Ai::PlotRoute(
		    *m_lanes.Galaxy(), PlaceOf(registry, context.entity), to,
		    Crossing(context.world, context.entity));
		if (!route)
			return Status::Failure;
		for (const Celestial::Exit &exit : route->jumps)
			trader.jumps.emplace_back(exit.jumpPoint);
		return Status::Success;
	}

	// Off the pad, unless the ship is at the station it is bound for: let go and lifted clear, or
	// a reservation or a place in a queue given up.
	Status Leave(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		const auto *docking = registry.try_get<Dock::Docking>(context.entity);
		if (!docking)
			return Status::Success;
		if (docking->station == BoundStation(registry, context.entity))
			return Status::Success;
		if (context.starting)
		{
			context.world.Apply(Dock::ReleasePad{SenderOf(registry, context.entity), context.entity});
			docking = registry.try_get<Dock::Docking>(context.entity);
			// Refused: the ship is held while its cargo crosses the deck.
			if (docking && docking->stage != Dock::Stage::Leaving)
				return Status::Failure;
		}
		return docking ? Status::Running : Status::Success;
	}

	Status JumpsLeft(TaskContext &context) const
	{
		return context.world.Registry().get<Trader>(context.entity).jumps.empty() ? Status::Failure
		                                                                          : Status::Success;
	}

	// Asks the next gate on the route to clear the ship through it, and waits where its cruise left
	// it while the ships before it in the gate's queue go through. Succeeds once it is cleared, and
	// fails if it is refused, or its place is given up.
	Status ClearGate(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		const Trader &trader = registry.get<Trader>(context.entity);
		if (trader.jumps.empty())
			return Status::Failure;
		const entt::entity gate = Ai::Gates::FindGate(
		    registry, registry.get<Sim::InSystem>(context.entity).system, trader.jumps.front());
		if (gate == entt::null)
			return Status::Failure;
		if (context.starting && !registry.all_of<Ai::Transit>(context.entity))
			context.world.Apply(
			    Ai::RequestTransit{SenderOf(registry, context.entity), context.entity, gate});
		const auto *transit = registry.try_get<Ai::Transit>(context.entity);
		if (!transit || transit->gate != gate)
			return Status::Failure;
		return transit->cleared ? Status::Success : Status::Running;
	}

	// Flies through the ring of the gate the ship is cleared through, which sends it along the lane:
	// in along the ring's axis from the side the ship is on, and on through towards the far side.
	// Succeeds once the gate has sent it, and fails if its clearance lapses or it passed through
	// unsent.
	Status FlyThrough(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		if (registry.all_of<Ai::OnLane>(context.entity))
			return Status::Success;
		const auto *transit = registry.try_get<Ai::Transit>(context.entity);
		if (!transit || !transit->cleared)
			return Status::Failure;
		if (!context.starting && registry.all_of<Ai::Destination>(context.entity))
			return Status::Running;
		// In the gate's frame, where its cruise left it, whose origin is the gate.
		const Sim::Transform &ring = registry.get<Sim::Transform>(transit->gate);
		const glm::dvec3 axis = ring.orientation * glm::dvec3{0.0, 1.0, 0.0};
		const glm::dvec3 at = registry.get<Sim::Transform>(context.entity).position;
		const double side = glm::dot(at - ring.position, axis) < 0.0 ? -1.0 : 1.0;
		Ai::Destination flight;
		flight.waypoints.push_back(axis * (side * kRingApproach));
		flight.offset = axis * (-side * kRingApproach);
		context.world.Apply(
		    Ai::SetDestination{SenderOf(registry, context.entity), context.entity, std::move(flight)});
		return Status::Running;
	}

	// Appends to the flight the waypoints of a course from one point to another round a station's
	// sphere, in the frame whose origin is given.
	void AddCourseRound(
	    Ai::Destination &flight, const entt::registry &registry, entt::entity station,
	    const glm::dvec3 &from, const glm::dvec3 &to, const glm::dvec3 &origin) const
	{
		const Ai::Sphere sphere{
		    registry.get<Sim::Transform>(station).position,
		    m_physics.ShapeRadius(registry.get<Physics::Body>(station).shape)};
		const Ai::Waypoints waypoints{{sphere}, kStationClearance};
		if (const std::optional<Ai::Course> course = waypoints.PlotCourse(from, to))
			for (std::size_t point = 1; point + 1 < course->points.size(); ++point)
				flight.waypoints.push_back(course->points[point] - origin);
	}

	// Crosses the system to the body the argument names, the gate the route goes through next ("gate")
	// or the market's station ("market"): flown out of any station's reach, round the station, where
	// a cruise does not engage, then taken across by the cruise, and succeeding as it ends. Fails when
	// the cruise is refused, or ends short of the body.
	Status CruiseTo(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		const Trader &trader = registry.get<Trader>(context.entity);
		const bool gate = !context.arguments.empty() && context.arguments[0] == "gate";
		const std::uint64_t system = registry.get<Sim::InSystem>(context.entity).system;
		const Celestial::StarSystem *starSystem = SystemOf(system);
		if (!starSystem || (gate && trader.jumps.empty()))
			return Status::Failure;
		const entt::entity station = BoundStation(registry, context.entity);
		if (!gate && station == entt::null)
			return Status::Failure;
		if (!gate && Dock::IsDocked(registry, context.entity, station))
			return Status::Success;
		const std::string name =
		    gate ? trader.jumps.front() : registry.get<Market>(trader.bound).station;
		const int index = starSystem->Find(name);
		if (index < 0)
			return Status::Failure;
		if (registry.all_of<Ai::Cruising>(context.entity))
			return Status::Running;

		const double now = context.world.Clock().Now();
		const std::vector<Celestial::SystemPosition> bodies =
		    Celestial::EvaluateSystem(*starSystem, now);
		const glm::dvec3 at = registry.get<Sim::Transform>(context.entity).position;
		const glm::dvec3 target = bodies[static_cast<std::size_t>(index)].metres;
		const bool there =
		    glm::distance(at, target) <
		    Ai::CruiseTravel::ArrivalDistance(starSystem->bodies[static_cast<std::size_t>(index)]) +
		        Ai::CruiseTravel::kStationReach;
		const auto *destination = registry.try_get<Ai::Destination>(context.entity);
		// Not flying out, and not cruising: it was, and has come out.
		if (!context.starting && !destination)
			return there ? Status::Success : Status::Failure;
		if (context.starting && there)
			return Status::Success;

		// The station whose reach the ship is within, if any.
		const Celestial::Body *near = nullptr;
		std::size_t nearIndex = 0;
		for (std::size_t each = 0; each < starSystem->bodies.size(); ++each)
			if (const Celestial::Body &body = starSystem->bodies[each];
			    body.kind == Celestial::BodyKind::Station &&
			    glm::distance(at, bodies[each].metres) < body.radius + Ai::CruiseTravel::kStationReach)
			{
				near = &body;
				nearIndex = each;
				break;
			}
		if (near)
		{
			// Already flying out, from an earlier tick.
			if (destination && !context.starting)
				return Status::Running;
			const auto *frame = registry.try_get<Physics::LocalFrame>(context.entity);
			const entt::entity leaving = FindStation(registry, system, near->name);
			if (!frame || leaving == entt::null)
				return Status::Failure;
			// A point just beyond its reach, on the side of the body to cruise to.
			const glm::dvec3 centre = bodies[nearIndex].metres;
			const glm::dvec3 way = target - centre;
			const glm::dvec3 out =
			    glm::length(way) > 1.0 ? glm::normalize(way) : glm::dvec3{0.0, 0.0, 1.0};
			const glm::dvec3 clear =
			    centre + out * (near->radius + Ai::CruiseTravel::kStationReach + kReachMargin);
			const glm::dvec3 origin = m_physics.FrameOrigin(system, frame->body, now);
			Ai::Destination flight;
			flight.offset = clear - origin;
			AddCourseRound(flight, registry, leaving, at, clear, origin);
			context.world.Apply(
			    Ai::SetDestination{
			        SenderOf(registry, context.entity), context.entity, std::move(flight)});
			return Status::Running;
		}

		context.world.Apply(Ai::Cruise{SenderOf(registry, context.entity), context.entity, name});
		return registry.all_of<Ai::Cruising>(context.entity) ? Status::Running : Status::Failure;
	}

	// Along the lane the gate sent the ship down, and out at the far end.
	Status Jump(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		Trader &trader = registry.get<Trader>(context.entity);
		if (registry.all_of<Ai::OnLane>(context.entity))
			return Status::Running;
		if (context.starting || trader.jumps.empty())
			return Status::Failure;
		trader.jumps.erase(trader.jumps.begin());
		return Status::Success;
	}

	// Onto a pad of the station the ship is bound for: asked for and flown in by the docks, or,
	// with every pad it fits taken, held off the station in its place in the queue until one frees.
	Status RequestDock(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		const entt::entity station = BoundStation(registry, context.entity);
		if (station == entt::null)
			return Status::Failure;
		if (Dock::IsDocked(registry, context.entity, station))
			return Status::Success;
		const auto *docking = registry.try_get<Dock::Docking>(context.entity);
		if (context.starting && !docking)
		{
			context.world.Apply(
			    Dock::RequestPad{SenderOf(registry, context.entity), context.entity, station, true});
			docking = registry.try_get<Dock::Docking>(context.entity);
		}
		// Refused, or its reservation lapsed.
		if (!docking || docking->station != station)
			return Status::Failure;
		if (docking->stage == Dock::Stage::Queued && !registry.all_of<Ai::Destination>(context.entity))
			HoldOff(context, station);
		return Status::Running;
	}

	// Buys the run's crates, as many as the hold, the purse and the market allow now, or sells every
	// crate of the good the hold has; the crates then cross the deck.
	Status MakeTrade(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		Trader &trader = registry.get<Trader>(context.entity);
		const App::Hold &hold = registry.get<App::Hold>(context.entity);
		const std::int64_t before = CreditsOf(registry, context.entity);
		const bool buying = !context.arguments.empty() && context.arguments[0] == "buy";
		const entt::entity market = buying ? trader.buyAt : trader.sellAt;
		if (market == entt::null || !registry.valid(market))
			return Status::Failure;
		std::int64_t crates = 0;
		if (buying)
		{
			const MarketGood *good = FindGood(registry.get<Market>(market), trader.good);
			if (!good)
				return Status::Failure;
			// What the route on to where it sells charges is kept back, so the ship can get there.
			const std::optional<Ai::Route> route =
			    m_lanes.Galaxy() && registry.valid(trader.sellAt)
			        ? Ai::PlotRoute(
			              *m_lanes.Galaxy(), PlaceOf(registry, context.entity),
			              MarketPlace(registry, trader.sellAt), Crossing(context.world, context.entity))
			        : std::nullopt;
			const std::int64_t spend = before - (route ? Tolls(*route) : 0);
			crates = std::min(
			    {hold.capacity - hold.Used(), good->stock,
			     std::max<std::int64_t>(spend, 0) / std::max<std::int64_t>(good->ask, 1)});
		}
		else
			crates = -hold.Crates(trader.good);
		if (crates == 0)
			return Status::Failure;
		const std::int64_t held = hold.Crates(trader.good);
		context.world.Apply(
		    App::Trade{
		        SenderOf(registry, context.entity), context.entity, market, trader.good, crates});
		if (registry.get<App::Hold>(context.entity).Crates(trader.good) == held)
			return Status::Failure;
		const std::int64_t credits = CreditsOf(registry, context.entity) - before;
		if (buying)
			trader.paid = -credits;
		else
		{
			trader.earned += credits - trader.paid;
			++trader.runs;
			Core::Log(
			    Core::Severity::Info, kTraderLog, "Trader {:#x} sold {} crates of {} for {} credits",
			    entt::to_integral(context.entity), -crates, trader.good, credits);
		}
		return Status::Success;
	}

	Status WaitCargo(TaskContext &context) const
	{
		return Dock::ToCarry(context.world.Registry(), context.entity) > 0 ? Status::Running
		                                                                   : Status::Success;
	}

	// Holds a queued ship off the station, on the side it came from, its place in the queue along
	// the line across that side, so the ships waiting do not wait in each other.
	void HoldOff(TaskContext &context, entt::entity station) const
	{
		entt::registry &registry = context.world.Registry();
		const Sim::Transform &at = registry.get<Sim::Transform>(station);
		const glm::dvec3 away = glm::conjugate(at.orientation) *
		                        (registry.get<Sim::Transform>(context.entity).position - at.position);
		const glm::dvec3 out =
		    glm::length(away) > 1.0 ? glm::normalize(away) : glm::dvec3{0.0, 0.0, 1.0};
		const glm::dvec3 across = glm::normalize(
		    std::abs(out.y) < 0.9 ? glm::cross(out, glm::dvec3{0.0, 1.0, 0.0})
		                          : glm::cross(out, glm::dvec3{1.0, 0.0, 0.0}));
		const std::vector<entt::entity> &queue = registry.get<Dock::Station>(station).queue;
		const auto place =
		    static_cast<double>(std::ranges::find(queue, context.entity) - queue.begin());
		const double radius = m_physics.ShapeRadius(registry.get<Physics::Body>(station).shape);
		Ai::Destination holding;
		holding.entity = station;
		holding.offset = out * (radius + kHoldingOff) + across * (place * kHoldingApart);
		context.world.Apply(
		    Ai::SetDestination{SenderOf(registry, context.entity), context.entity, std::move(holding)});
	}

	// The lanes' tolls along a route.
	static std::int64_t Tolls(const Ai::Route &route)
	{
		std::int64_t tolls = 0;
		for (const Celestial::Exit &exit : route.jumps)
			tolls += exit.toll;
		return tolls;
	}

	// The owner's credits, which the ship trades with.
	static std::int64_t CreditsOf(const entt::registry &registry, entt::entity ship)
	{
		const Purse *purse = PurseOf(registry, ship);
		return purse ? purse->credits : 0;
	}

	static Sim::Controller SenderOf(const entt::registry &registry, entt::entity ship)
	{
		const auto *controlled = registry.try_get<Sim::ControlledBy>(ship);
		return controlled ? controlled->controller : Sim::Controller{};
	}

	static entt::entity BoundStation(const entt::registry &registry, entt::entity ship)
	{
		const entt::entity bound = registry.get<Trader>(ship).bound;
		const auto *market =
		    bound != entt::null && registry.valid(bound) ? registry.try_get<Market>(bound) : nullptr;
		return market ? market->stationEntity : entt::null;
	}

	const Celestial::StarSystem *SystemOf(std::uint64_t system) const
	{
		const auto found = std::ranges::find(
		    m_systems, system,
		    [](const Celestial::StarSystem &each) { return Sim::SystemId(each.name); });
		return found != m_systems.end() ? &*found : nullptr;
	}

	std::size_t SystemIndex(std::uint64_t system) const
	{
		const auto &systems = m_lanes.Galaxy()->systems;
		const auto found = std::ranges::find(
		    systems, system,
		    [](const Celestial::GalaxySystem &each) { return Sim::SystemId(each.name); });
		return static_cast<std::size_t>(found - systems.begin());
	}

	// Where the ship is, as a route starts: the station it is docked at, or its system.
	Ai::Place PlaceOf(const entt::registry &registry, entt::entity ship) const
	{
		Ai::Place place{SystemIndex(registry.get<Sim::InSystem>(ship).system), {}};
		if (const auto *docking = registry.try_get<Dock::Docking>(ship);
		    docking && docking->stage == Dock::Stage::Docked)
			place.body = registry.get<Physics::OnRails>(docking->station).body;
		return place;
	}

	Ai::Place MarketPlace(const entt::registry &registry, entt::entity market) const
	{
		const Market &found = registry.get<Market>(market);
		return {SystemIndex(found.system), found.station};
	}

	// What a crossing of a system costs the ship: its drive's time for the course's length, and what
	// the flights out of a station's reach and in to a gate come to.
	Ai::CrossingCost Crossing(const Sim::SimWorld &world, entt::entity ship) const
	{
		const Ai::CruiseDrive drive = world.Registry().get<Ai::CruiseDrive>(ship);
		return Ai::CrossingByCourse(
		    m_systems, world.Clock().Now(), [drive](double metres)
		    { return metres > 0.0 ? Ai::CruiseSeconds(drive, metres) + kCruiseHandlingSeconds : 0.0; },
		    Ai::CruiseTravel::kCourseClearance);
	}

	const Physics::PhysicsWorld &m_physics;
	const Ai::LaneTravel &m_lanes;
	std::vector<Celestial::StarSystem> m_systems;
	Ai::DecisionDefinition m_decision;
	Script::ScriptHost *m_pScripts;
};

// The sample's traders, as the stations' parked ships are listed (Stations.cpp).
struct Start
{
	std::string_view station;
	std::uint32_t pad;
	std::string_view hull;
	float mass;
	std::int64_t hold;
	std::int64_t credits;
};
constexpr std::array<Start, 7> kTraders{{
    {"Ostra Highport", 3, "transport", 1.5e6f, 40, 600000},
    {"Ostra Highport", 4, "transport", 1.5e6f, 40, 600000},
    {"Ostra Highport", 6, "shuttle", 2.0e4f, 20, 250000},
    {"Ostra Highport", 9, "transport", 1.5e6f, 40, 600000},
    {"Ostra Highport", 10, "transport", 1.5e6f, 40, 600000},
    {"Corva Refinery", 1, "transport", 1.5e6f, 40, 600000},
    {"Corva Refinery", 5, "shuttle", 2.0e4f, 20, 250000},
}};
} // namespace

void RegisterTraders(
    Sim::SimWorld &world, Ai::Behaviours &behaviours, const Physics::PhysicsWorld &physics,
    const Ai::LaneTravel &lanes, std::vector<Celestial::StarSystem> systems,
    const Ai::DecisionDefinition &decision, Script::ScriptHost *pScripts)
{
	RegisterTypes();
	world.RegisterComponent<Trader>();
	world.ShowInSnapshots<Trader>();
	const auto round = std::make_shared<Round>(physics, lanes, std::move(systems), decision, pScripts);
	round->Define(behaviours, round);
}

std::optional<std::string> UnansweredInput(
    const Ai::DecisionDefinition &decision, const Script::ScriptHost *pScripts)
{
	for (const Ai::UtilityAction &action : decision.actions)
		for (const Ai::Consideration &consideration : action.considerations)
			if (std::ranges::find(kTraderInputs, consideration.input) == kTraderInputs.end() &&
			    !(pScripts && pScripts->Answers(consideration.input)))
				return consideration.input;
	return std::nullopt;
}

void LoadTraderDefinitions(Sim::DefinitionRegistry &definitions)
{
	Ai::RegisterTypes();
	definitions.Load<Ai::DecisionDefinition>(
	    Ai::kDecisionDirectory, Serialise::FormatVersion{Ai::kDecisionVersion});
	definitions.Load<Ai::BehaviourDefinition>(
	    Ai::kBehaviourDirectory, Serialise::FormatVersion{Ai::kBehaviourVersion});
	for (const std::string &name : definitions.Names<Ai::DecisionDefinition>())
		Ai::CheckDecision(name, *definitions.Find<Ai::DecisionDefinition>(name));
}

Sim::Controller TraderController(std::uint32_t number)
{
	return {Sim::ControllerKind::Ai, number};
}

std::vector<entt::entity> OpenTraders(Sim::SimWorld &world, Dock::Docks &docks)
{
	std::vector<entt::entity> traders;
	entt::registry &registry = world.Registry();
	for (std::size_t index = 0; index < kTraders.size(); ++index)
	{
		const Start &start = kTraders[index];
		entt::entity station = entt::null;
		for (const auto [entity, built, rails] :
		     registry.view<const Dock::Station, const Physics::OnRails>().each())
			if (rails.body == start.station)
				station = entity;
		if (station == entt::null || !docks.FindHull(start.hull))
			continue;
		const entt::entity pad = registry.get<Dock::Station>(station).pads.at(start.pad - 1);
		if (registry.get<Dock::Pad>(pad).state != Dock::PadState::Free)
		{
			Core::Log(
			    Core::Severity::Warning, kTraderLog, "{}'s pad {} is taken; its trader is left out",
			    start.station, start.pad);
			continue;
		}
		const entt::entity ship = registry.create();
		registry.emplace<Sim::Transform>(ship, registry.get<Sim::Transform>(station));
		registry.emplace<Sim::InSystem>(ship, registry.get<Sim::InSystem>(station).system);
		registry.emplace<Physics::LocalFrame>(ship, std::string{start.station});
		MakeShip(registry, docks, ship, start.hull, start.mass, true);
		registry.emplace<Ai::Steered>(ship);
		// Each trader the owner of its one ship.
		const Sim::Controller controller = TraderController(static_cast<std::uint32_t>(index));
		registry.emplace<Sim::ControlledBy>(ship, controller);
		registry.emplace<App::Hold>(ship, App::Hold{start.hold, {}});
		if (FindOwner(registry, controller) == entt::null)
			MakeOwner(registry, controller, start.credits);
		registry.emplace<Trader>(ship);
		registry.emplace<Ai::Behaviour>(ship, Ai::Behaviour{std::string{kTraderTree}, {}, {}});
		docks.Park(world, ship, pad);
		traders.push_back(ship);
	}
	return traders;
}
} // namespace Jettison::App
