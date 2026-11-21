#include "../src/Traders.h"
#include "../src/Trading.h"
#include "SampleGalaxy.h"
#include "Stations.h"
#include <jettison/app/Mounts.h>

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <jettison/ai/BehaviourTree.h>
#include <jettison/ai/Steering.h>
#include <jettison/ai/UtilityAi.h>
#include <jettison/celestial/Galaxy.h>
#include <jettison/celestial/StarSystem.h>
#include <jettison/core/Log.h>
#include <jettison/dock/Docks.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/script/ScriptHost.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>
#include <jettison/space/Cruise.h>
#include <jettison/space/Gates.h>
#include <jettison/space/Lanes.h>
#include <jettison/spatial/SystemIndices.h>
#include <jettison/vfs/FileSystem.h>

using namespace Jettison;

using Testing::Galaxy;
using Testing::kStep;
using Testing::ReadAsset;


TEST_CASE(
    "Traders: the sample's tree and decision load, and the traders open docked, with nothing chosen")
{
	Galaxy galaxy;
	CHECK(galaxy.behaviours.HasTree(Sample::kTraderTree));
	CHECK(galaxy.behaviours.NodeCount(Sample::kTraderTree) == 35);
	REQUIRE(galaxy.traders.size() == 7);
	const entt::registry &registry = galaxy.world.Registry();
	for (const entt::entity ship : galaxy.traders)
	{
		CHECK(registry.get<Dock::Docking>(ship).stage == Dock::Stage::Docked);
		CHECK(registry.get<Sim::ControlledBy>(ship).controller.kind == Sim::ControllerKind::Ai);
		CHECK(registry.get<Ai::Behaviour>(ship).tree == Sample::kTraderTree);
		CHECK(galaxy.TraderOf(ship).good.empty());
	}
	// Corva's last free pad, a frigate, is all that is left for the traders that come to it.
	const entt::entity corva = Sample::FindStation(registry, Sim::FrameId("Vesper"), "Corva Refinery");
	std::size_t free = 0;
	for (const entt::entity pad : registry.get<Dock::Station>(corva).pads)
		free += registry.get<Dock::Pad>(pad).state == Dock::PadState::Free ? 1 : 0;
	CHECK(free == 1);
	// A pad that is taken is left alone, and its trader left out.
	CHECK(Sample::OpenTraders(galaxy.world, galaxy.docks, galaxy.drives).empty());
}


TEST_CASE("Traders: each chooses the run worth most, steered off what its rivals chose, or waits")
{
	// Alone, the first trader buys what it would; with its rivals all after that, it looks elsewhere.
	Galaxy alone;
	entt::registry &registry = alone.world.Registry();
	for (std::size_t other = 1; other < alone.traders.size(); ++other)
		registry.remove<Ai::Behaviour>(alone.traders[other]);
	alone.Step();
	const Sample::Trader first = alone.TraderOf(alone.traders[0]);
	REQUIRE_FALSE(first.good.empty());
	CHECK(first.buyAt != first.sellAt);
	CHECK(first.sellAt != entt::null);
	MESSAGE(
	    "Alone, it buys ", first.good, " at ", registry.get<Sample::Market>(first.buyAt).station,
	    " to sell at ", registry.get<Sample::Market>(first.sellAt).station);

	Galaxy crowded;
	entt::registry &crowd = crowded.world.Registry();
	for (std::size_t other = 1; other < crowded.traders.size(); ++other)
	{
		crowd.remove<Ai::Behaviour>(crowded.traders[other]);
		Sample::Trader &rival = crowd.get<Sample::Trader>(crowded.traders[other]);
		rival.good = first.good;
		rival.buyAt = first.buyAt;
		rival.sellAt = first.sellAt;
	}
	crowded.Step();
	const Sample::Trader &steered = crowded.TraderOf(crowded.traders[0]);
	REQUIRE_FALSE(steered.good.empty());
	CHECK((steered.good != first.good || steered.buyAt != first.buyAt));
	MESSAGE(
	    "Crowded, it buys ", steered.good, " at ", crowd.get<Sample::Market>(steered.buyAt).station);

	// Run all together, the first step's choices spread over the shelves.
	Galaxy galaxy;
	galaxy.Step();
	std::set<std::pair<std::string, entt::entity>> chosen;
	for (const entt::entity ship : galaxy.traders)
		chosen.insert({galaxy.TraderOf(ship).good, galaxy.TraderOf(ship).buyAt});
	CHECK(chosen.size() >= 3);

	// A trader with no credits has nothing worth flying, and waits.
	Galaxy broke;
	Sample::PurseOf(broke.world.Registry(), broke.traders[0])->credits = 0;
	broke.Step();
	CHECK(broke.TraderOf(broke.traders[0]).good.empty());
	CHECK(broke.world.Registry().get<Sample::Hold>(broke.traders[0]).cargo.empty());

	// One with a good in its hold already sells it, at the market that pays most for it.
	Galaxy laden;
	entt::registry &ladenWorld = laden.world.Registry();
	ladenWorld.get<Sample::Hold>(laden.traders[0]).cargo.push_back({"copper", 5});
	laden.Step();
	const Sample::Trader &seller = laden.TraderOf(laden.traders[0]);
	CHECK(seller.good == "copper");
	CHECK(seller.buyAt == entt::null);
	CHECK(seller.sellAt != entt::null);
}


TEST_CASE("Traders: a mod's script, from the user's mods, weighs their runs and hears their trades")
{
	const std::filesystem::path user = std::filesystem::path{JETTISON_APP_TEST_OUTPUT} / "traders-user";
	std::filesystem::remove_all(user);
	std::filesystem::create_directories(user / "mods");
	std::filesystem::copy(
	    JETTISON_SMOKE_MOD_DIR, user / "mods/smoke", std::filesystem::copy_options::recursive);

	std::vector<std::string> heard;
	const Core::LogCallbackId listening = Core::AddLogCallback(
	    [&heard](const Core::LogRecord &record)
	    {
		    if (record.category == "script" && record.message.starts_with("smoke: heard"))
			    heard.emplace_back(record.message);
	    });
	{
		Galaxy galaxy{true, user};
		REQUIRE(galaxy.pScripts->LoadedModules() == 1);
		// Every trader chooses in the first step, each run weighed by the script's input as well.
		galaxy.Step();
		const std::uint64_t chosen = galaxy.pScripts->Scripts().front().calls;
		CHECK(chosen >= galaxy.traders.size());
		for (int step = 0; step < 60 * 120 && galaxy.Events<Sample::Traded>().empty(); ++step)
			galaxy.Step();
		const std::vector<Sample::Traded> traded = galaxy.Events<Sample::Traded>();
		REQUIRE_FALSE(traded.empty());
		CHECK(heard.size() == traded.size());
		CHECK(galaxy.pScripts->Scripts().front().traps == 0);
		MESSAGE("The script answered ", chosen, " inputs in the first step, and heard ", heard.front());
	}
	Core::RemoveLogCallback(listening);
	std::filesystem::remove_all(user);
}


TEST_CASE(
    "Traders: the sample's traders fly their runs across the lane, docking in turn, touching nothing")
{
	// Two hours: every trader's runs, out of one system and into the others, each way cruised from a
	// station to the gate and from the gate to the market.
	constexpr int kSteps = 60 * 60 * 120;
	Galaxy galaxy;
	entt::registry &registry = galaxy.world.Registry();
	const auto isShip = [&](entt::entity entity)
	{ return registry.valid(entity) && registry.all_of<Dock::Hull>(entity); };
	const auto isStation = [&](entt::entity entity)
	{ return registry.valid(entity) && registry.all_of<Dock::Station>(entity); };

	// Each contact a ship began with a station, and whether it was the hull on its own pad: on the
	// pad's face within its size, as a ship let go lies on it.
	std::size_t onPads = 0;
	std::vector<std::string> collisions;
	double nearest = std::numeric_limits<double>::max();
	std::map<entt::entity, std::uint64_t> systemOf;
	std::size_t crossings = 0;
	// How near the hull of a ship that came out of a lane or a cruise was to any other's, in the step
	// it arrived.
	double tightest = std::numeric_limits<double>::max();
	std::size_t arrivals = 0;
	for (int step = 0; step < kSteps; ++step)
	{
		const std::size_t before = galaxy.contacts.size();
		const std::size_t seen = galaxy.events.size();
		galaxy.Step();
		for (std::size_t index = seen; index < galaxy.events.size(); ++index)
		{
			entt::entity arrived = entt::null;
			if (const auto *ended = galaxy.events[index].event.try_cast<Space::CruiseEnded>())
				arrived = ended->ship;
			else if (const auto *left = galaxy.events[index].event.try_cast<Space::LaneLeft>())
				arrived = left->entity;
			if (arrived == entt::null)
				continue;
			++arrivals;
			const std::uint64_t system = registry.get<Sim::InFrame>(arrived).frame;
			const double radius =
			    galaxy.physics.ShapeRadius(registry.get<Physics::Body>(arrived).shape);
			for (const auto [other, body, inSystem, transform] :
			     registry.view<const Physics::Body, const Sim::InFrame, const Sim::Transform>().each())
				if (other != arrived && inSystem.frame == system && !body.sensor)
					tightest = std::min(
					    tightest,
					    glm::distance(
					        registry.get<Sim::Transform>(arrived).position, transform.position) -
					        radius - galaxy.physics.ShapeRadius(body.shape));
		}
		for (std::size_t index = before; index < galaxy.contacts.size(); ++index)
		{
			const Physics::ContactEvent &contact = galaxy.contacts[index];
			const entt::entity ship = isShip(contact.first) ? contact.first : contact.second;
			const entt::entity other = ship == contact.first ? contact.second : contact.first;
			if (isShip(ship) && isStation(other))
				if (const auto *docking = registry.try_get<Dock::Docking>(ship);
				    docking && docking->pad != entt::null)
				{
					const Sim::Transform &pad = registry.get<Sim::Transform>(docking->pad);
					const glm::dvec3 at =
					    glm::conjugate(pad.orientation) * (contact.point - pad.position);
					const glm::vec2 size =
					    galaxy.docks.FindPadClass(registry.get<Dock::Pad>(docking->pad).padClass)
					        ->padSize;
					if (std::abs(at.y) < 0.5 && std::abs(at.x) < size.x / 2.0 &&
					    std::abs(at.z) < size.y / 2.0)
					{
						++onPads;
						continue;
					}
				}
			collisions.push_back(
			    std::format(
			        "step {}: {:#x} and {:#x} at {}", step, entt::to_integral(contact.first),
			        entt::to_integral(contact.second), contact.speed));
		}
		// The traders' hulls apart, each pair in the same system and flying.
		if (step % 30 == 0)
			for (std::size_t a = 0; a < galaxy.traders.size(); ++a)
				for (std::size_t b = a + 1; b < galaxy.traders.size(); ++b)
				{
					const entt::entity first = galaxy.traders[a];
					const entt::entity second = galaxy.traders[b];
					if (registry.get<Sim::InFrame>(first).frame !=
					        registry.get<Sim::InFrame>(second).frame ||
					    registry.get<Sim::InFrame>(first).frame == Sim::kNoFrame ||
					    registry.all_of<Dock::Docking>(first) ||
					    registry.all_of<Dock::Docking>(second) ||
					    registry.all_of<Space::Cruising>(first) ||
					    registry.all_of<Space::Cruising>(second))
						continue;
					const double apart =
					    glm::distance(
					        registry.get<Sim::Transform>(first).position,
					        registry.get<Sim::Transform>(second).position) -
					    galaxy.physics.ShapeRadius(registry.get<Physics::Body>(first).shape) -
					    galaxy.physics.ShapeRadius(registry.get<Physics::Body>(second).shape);
					nearest = std::min(nearest, apart);
				}
		for (const entt::entity ship : galaxy.traders)
		{
			const std::uint64_t system = registry.get<Sim::InFrame>(ship).frame;
			if (system != Sim::kNoFrame && systemOf.contains(ship) && systemOf[ship] != system)
				++crossings;
			if (system != Sim::kNoFrame)
				systemOf[ship] = system;
		}
	}

	// Every trader sold a run's crates, and made something by it, across the lane.
	std::uint32_t runs = 0;
	for (const entt::entity ship : galaxy.traders)
	{
		const Sample::Trader &trader = galaxy.TraderOf(ship);
		CAPTURE(entt::to_integral(ship));
		CHECK(trader.runs >= 1);
		CHECK(trader.earned > 0);
		runs += trader.runs;
	}
	CHECK(crossings >= galaxy.traders.size());
	CHECK(galaxy.Events<Space::LaneEntered>().size() >= galaxy.traders.size());
	// Corva's one free pad made a queue, which was served.
	CHECK_FALSE(galaxy.Events<Dock::PadQueued>().empty());
	// Every crate bought and sold crossed the deck: a transfer done for each trade.
	CHECK(
	    galaxy.Events<Dock::CargoCarried>().size() >=
	    galaxy.Events<Sample::Traded>().size() - galaxy.traders.size());
	CHECK(galaxy.Events<Sample::TradeRefused>().empty());
	CHECK(galaxy.Events<Dock::PadRefused>().empty());
	CHECK(galaxy.Events<Space::LaneRefused>().empty());
	// Every lane was entered through a gate's ring, its ship cleared and its owner charged the toll;
	// no clearance was refused or lapsed, and no ship flew through a ring it was not cleared through.
	const std::vector<Space::TransitSent> sent = galaxy.Events<Space::TransitSent>();
	CHECK(sent.size() == galaxy.Events<Space::LaneEntered>().size());
	std::int64_t tolls = 0;
	for (const Space::TransitSent &transit : sent)
	{
		CHECK(transit.toll > 0);
		tolls += transit.toll;
	}
	CHECK(galaxy.Events<Space::TransitRefused>().empty());
	CHECK(galaxy.Events<Space::TransitLapsed>().empty());
	CHECK(galaxy.Events<Space::GatePassed>().empty());
	// How long a clearance waited to be flown through, against the two minutes it is held for.
	std::map<entt::entity, std::uint64_t> clearedAt;
	std::uint64_t longestFlight = 0;
	for (const Sim::SimEvent &event : galaxy.events)
		if (const auto *cleared = event.event.try_cast<Space::TransitCleared>())
			clearedAt[cleared->ship] = event.step;
		else if (const auto *through = event.event.try_cast<Space::TransitSent>())
			longestFlight = std::max(longestFlight, event.step - clearedAt.at(through->ship));
	MESSAGE(
	    sent.size(), " ships sent through the gates for ", tolls, " credits of tolls, ",
	    galaxy.Events<Space::TransitQueued>().size(),
	    " waiting their turn; the longest from cleared to sent ",
	    static_cast<double>(longestFlight) * kStep, " s");
	// Every ship crossed a system by cruise, in both directions of each run, and never was refused;
	// every arrival, by cruise or lane, came out clear of the ships there.
	CHECK(galaxy.Events<Space::CruiseStarted>().size() >= 2 * runs);
	CHECK(galaxy.Events<Space::CruiseRefused>().empty());
	CHECK(arrivals >= galaxy.Events<Space::CruiseEnded>().size());
	CHECK(tightest >= Space::LaneTravel::kArrivalClearance);
	double shortest = std::numeric_limits<double>::max();
	double longest = 0.0;
	for (const Space::CruiseStarted &started : galaxy.Events<Space::CruiseStarted>())
	{
		shortest = std::min(shortest, started.seconds);
		longest = std::max(longest, started.seconds);
	}
	MESSAGE(
	    arrivals, " arrivals, the tightest ", tightest, " m between hulls; ",
	    galaxy.Events<Space::CruiseStarted>().size(), " cruises of ", shortest, " to ", longest, " s");
	MESSAGE(
	    runs, " runs sold in ", kSteps / 3600, " minutes, ", crossings, " crossings, ",
	    galaxy.Events<Dock::PadQueued>().size(), " places queued, ", onPads,
	    " contacts on their pads as they were let go; nearest flying hulls ", nearest, " m apart");
	// The traders' trades moved the galaxy's means off the goods' base prices.
	const Sample::GalacticPrices *prices = Sample::FindGalacticPrices(registry);
	REQUIRE(prices);
	std::string means;
	std::size_t moved = 0;
	for (const Sample::GalacticPrice &price : prices->goods)
	{
		means += std::format(" {} {}", price.good, price.mean);
		for (const auto [entity, market] : registry.view<const Sample::Market>().each())
			if (const auto good =
			        std::ranges::find(market.goods, price.good, &Sample::MarketGood::good);
			    good != market.goods.end())
			{
				moved += price.mean != good->basePrice ? 1 : 0;
				break;
			}
	}
	CHECK(moved > 0);
	MESSAGE(moved, " galactic means moved:", means);
	for (const std::string &collision : collisions)
		MESSAGE(collision);
	CHECK(collisions.empty());
}


TEST_CASE("Traders: a world saved part-way through the traders' rounds carries on as the one saved")
{
	constexpr int kSaveAt = 60 * 60 * 4;
	constexpr int kSteps = 60 * 60 * 3;
	Galaxy original;
	for (int step = 0; step < kSaveAt; ++step)
		original.Step();
	const std::vector<std::byte> save = original.world.Save();
	for (int step = 0; step < kSteps; ++step)
		original.Step();

	Galaxy loaded{false};
	loaded.world.Load(save);
	for (int step = 0; step < kSteps; ++step)
		loaded.Step();
	CHECK(loaded.world.StateHash() == original.world.StateHash());
	// The same in every build: pinned in Debug, and checked in Release. Re-pinned in W01 for a pad's
	// class saved as its name, the old build's saves loaded and saved again the same; and for the
	// classes' ship names and a docking's feature, as the dock's approach was; and in W05 for a ship's
	// controls let go with its destination as it sets out on a cruise or a lane; and for the
	// fast-forward, a kinematic body put at rest where it is while nothing dynamic shares its world,
	// half an hour of every trader's place, runs and cargo and every market the same to the bit; and
	// for the way in to a pad stopped out along its normal first, as the dock's approach was.
	CHECK(original.world.StateHash() == 0x91137B35E6F6F845ULL);
}
