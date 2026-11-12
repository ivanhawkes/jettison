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

// How near a gate's centre a trader is when it goes through: well inside its 500 m ring, still
// moving, as a ship flying through it is.
constexpr double kGateReach = 150.0;
// How far a trader holds off a station's sphere while it waits its turn for a pad, and how far apart
// along the line across it each place in the queue waits.
constexpr double kHoldingOff = 400.0;
constexpr double kHoldingApart = 150.0;
// What a trader allows for a stop at a station in its reckoning of a run: the approach round the
// station and onto the pad, and the cargo across the deck.
constexpr double kStopSeconds = 240.0;
// The speed a trader crosses a system at in that reckoning, under its thrusters' 150 m/s for the
// turns and the arrival, and the clearance its courses keep from a planet.
constexpr double kCrossingSpeed = 120.0;
constexpr double kCrossingClearance = 2000.0;
// A course off a station keeps this far from its sphere.
constexpr double kStationClearance = 100.0;

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
	    std::vector<Celestial::StarSystem> systems, Ai::DecisionDefinition decision)
	    : m_physics{physics}, m_lanes{lanes}, m_systems{std::move(systems)},
	      m_decision{std::move(decision)}
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
		task("fly-to-gate", &Round::FlyToGate);
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
		const Purse &purse = registry.get<Purse>(context.entity);
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
					     purse.credits / std::max<std::int64_t>(good.ask, 1)});
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

		// The run's time: from where the ship is to the market it buys at, and on to where it sells,
		// over the lanes and across each system by course, with a stop at each; worked out only
		// for a run still in the reckoning, and once for each pair of places.
		std::map<std::pair<std::string, std::string>, double> legs;
		std::optional<Ai::CrossingCost> crossing;
		const Ai::Place here = PlaceOf(registry, context.entity);
		const auto leg = [&](const Ai::Place &from, const Ai::Place &to)
		{
			const auto key = std::pair{
			    std::format("{}/{}", from.system, from.body), std::format("{}/{}", to.system, to.body)};
			if (const auto known = legs.find(key); known != legs.end())
				return known->second;
			if (!crossing)
				crossing = Crossing(context.world);
			const std::optional<Ai::Route> route =
			    Ai::PlotRoute(*m_lanes.Galaxy(), from, to, *crossing);
			const double seconds = route ? route->seconds + kStopSeconds : -1.0;
			legs.emplace(key, seconds);
			return seconds;
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
					double seconds = 0.0;
					if (run.buyAt != entt::null)
					{
						const Ai::Place buy = MarketPlace(registry, run.buyAt);
						const double there = leg(here, buy);
						const double on = leg(buy, sell);
						if (there < 0.0 || on < 0.0)
							return 0.0;
						seconds = there + on;
					}
					else
					{
						seconds = leg(here, sell);
						if (seconds < 0.0)
							return 0.0;
					}
					const std::int64_t gain = run.crates * (run.bid - run.ask);
					return static_cast<double>(gain) / std::max(seconds, 1.0);
				}
				throw std::invalid_argument{std::format(
				    "The trade decision asks for '{}', which a trader does not know", input)};
			};
			options.push_back({run.buyAt == entt::null ? "sell" : "trade", inputs});
		}
		options.push_back({"wait", [](std::string_view) { return 0.0; }});

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
		    *m_lanes.Galaxy(), PlaceOf(registry, context.entity), to, Crossing(context.world));
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

	// Flies to the next gate on the route, round the station the ship leaves, and succeeds once it is
	// within reach of the gate's centre. The gate turns about its station with the station's orbit,
	// a few metres a second in the station's frame, so the ship aims again as it closes.
	Status FlyToGate(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		const Trader &trader = registry.get<Trader>(context.entity);
		const auto *frame = registry.try_get<Physics::LocalFrame>(context.entity);
		if (trader.jumps.empty() || !frame)
			return Status::Failure;
		const std::uint64_t system = registry.get<Sim::InSystem>(context.entity).system;
		const Celestial::StarSystem *starSystem = SystemOf(system);
		const int gate = starSystem ? starSystem->Find(trader.jumps.front()) : -1;
		if (gate < 0)
			return Status::Failure;
		const double now = context.world.Clock().Now();
		const glm::dvec3 at = registry.get<Sim::Transform>(context.entity).position;
		const glm::dvec3 target =
		    Celestial::EvaluateBody(*starSystem, static_cast<std::size_t>(gate), now).metres;
		if (glm::distance(at, target) < kGateReach)
			return Status::Success;
		const glm::dvec3 origin = m_physics.FrameOrigin(system, frame->body, now);

		const auto *destination = registry.try_get<Ai::Destination>(context.entity);
		const bool coursed = destination && destination->next >= destination->waypoints.size();
		if (!context.starting && destination && destination->entity == entt::null &&
		    (!coursed || glm::distance(destination->offset, target - origin) < 25.0))
			return Status::Running;

		Ai::Destination flight;
		flight.offset = target - origin;
		// Round the station it leaves, the first time; later it only aims again.
		if (context.starting || !destination)
			if (const entt::entity station = FindStation(registry, system, frame->body);
			    station != entt::null)
			{
				const Ai::Sphere sphere{
				    registry.get<Sim::Transform>(station).position,
				    m_physics.ShapeRadius(registry.get<Physics::Body>(station).shape)};
				const Ai::Waypoints waypoints{{sphere}, kStationClearance};
				if (const std::optional<Ai::Course> course = waypoints.PlotCourse(at, target))
					for (std::size_t point = 1; point + 1 < course->points.size(); ++point)
						flight.waypoints.push_back(course->points[point] - origin);
			}
		context.world.Apply(Ai::SetDestination{context.entity, std::move(flight)});
		return Status::Running;
	}

	// Through the gate: along its lane, and out at the far end.
	Status Jump(TaskContext &context) const
	{
		entt::registry &registry = context.world.Registry();
		Trader &trader = registry.get<Trader>(context.entity);
		if (context.starting)
		{
			if (trader.jumps.empty())
				return Status::Failure;
			context.world.Apply(Ai::ClearDestination{context.entity});
			context.world.Apply(Ai::EnterLane{context.entity, trader.jumps.front()});
			if (!registry.all_of<Ai::OnLane>(context.entity))
				return Status::Failure;
		}
		if (registry.all_of<Ai::OnLane>(context.entity))
			return Status::Running;
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
		const std::int64_t before = registry.get<Purse>(context.entity).credits;
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
			crates = std::min(
			    {hold.capacity - hold.Used(), good->stock,
			     registry.get<Purse>(context.entity).credits / std::max<std::int64_t>(good->ask, 1)});
		}
		else
			crates = -hold.Crates(trader.good);
		if (crates == 0)
			return Status::Failure;
		const std::int64_t held = hold.Crates(trader.good);
		context.world.Apply(App::Trade{context.entity, market, trader.good, crates});
		if (registry.get<App::Hold>(context.entity).Crates(trader.good) == held)
			return Status::Failure;
		const std::int64_t credits = registry.get<Purse>(context.entity).credits - before;
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
		context.world.Apply(Ai::SetDestination{context.entity, std::move(holding)});
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

	Ai::CrossingCost Crossing(const Sim::SimWorld &world) const
	{
		return Ai::CrossingByCourse(m_systems, world.Clock().Now(), kCrossingSpeed, kCrossingClearance);
	}

	const Physics::PhysicsWorld &m_physics;
	const Ai::LaneTravel &m_lanes;
	std::vector<Celestial::StarSystem> m_systems;
	Ai::DecisionDefinition m_decision;
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
    const Ai::DecisionDefinition &decision)
{
	RegisterTypes();
	world.RegisterComponent<Trader>();
	world.ShowInSnapshots<Trader>();
	const auto round = std::make_shared<Round>(physics, lanes, std::move(systems), decision);
	round->Define(behaviours, round);
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
		MakeShip(registry, ship, start.hull, start.mass, true);
		registry.emplace<Ai::Steered>(ship);
		registry.emplace<Sim::ControlledBy>(ship, TraderController(static_cast<std::uint32_t>(index)));
		registry.emplace<App::Hold>(ship, App::Hold{start.hold, {}});
		registry.emplace<Purse>(ship, Purse{start.credits});
		registry.emplace<Trader>(ship);
		registry.emplace<Ai::Behaviour>(ship, Ai::Behaviour{std::string{kTraderTree}, {}, {}});
		docks.Park(world, ship, pad);
		traders.push_back(ship);
	}
	return traders;
}
} // namespace Jettison::App
