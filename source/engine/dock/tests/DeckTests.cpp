#include "Fixture.h"

#include <doctest/doctest.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace Jettison;
using Dock::PadClass;
using DockTests::Fixture;

namespace
{
constexpr std::int64_t kSteps60 = 60;

// Ostra Highport built to its layout, or to one of the tests', with ships parked on its pads.
struct Deck : Fixture
{
	entt::entity station{entt::null};
	int ships{0};
	// The nearest two buggies came, centre to centre, and the most out for one pad at once.
	double nearest{std::numeric_limits<double>::infinity()};
	std::size_t mostOut{0};
	// Whether every buggy was on the deck, a pier's top 10 m up, in the station's axes.
	bool onDeck{true};
	std::size_t buggySteps{0};

	explicit Deck(std::string_view layout = "ostra-highport")
	{
		if (layout != "ostra-highport")
		{
			Dock::StationLayout parked = *docks.FindLayout("ostra-highport");
			parked.traffic.working = false;
			docks.DefineLayout(std::string{layout}, parked);
		}
		station = docks.MakeStation(world, DockTests::kHalcyon, DockTests::kHighport, layout);
	}

	entt::entity Parked(std::uint32_t number, std::string_view hull = "shuttle")
	{
		const entt::entity ship = Ship(station, hull, {400.0 * ships++, 3000.0, 0.0});
		docks.Park(world, ship, PadNumbered(station, number));
		return ship;
	}

	// A step, and how the buggies stand after it.
	void Advance()
	{
		Step();
		const entt::registry &registry = world.Registry();
		const Sim::Transform &at = registry.get<Sim::Transform>(station);
		std::vector<glm::dvec3> places;
		std::map<entt::entity, std::size_t> out;
		for (const auto [entity, buggy, transform] :
		     registry.view<const Dock::Buggy, const Sim::Transform>().each())
		{
			const glm::dvec3 local =
			    glm::conjugate(at.orientation) * (transform.position - at.position);
			onDeck = onDeck && std::abs(local.y - 10.0) < 1.0e-6;
			places.push_back(local);
			++out[buggy.pad];
			++buggySteps;
		}
		for (const auto &[pad, count] : out)
			mostOut = std::max(mostOut, count);
		for (std::size_t first = 0; first < places.size(); ++first)
			for (std::size_t second = first + 1; second < places.size(); ++second)
				nearest = std::min(nearest, glm::distance(places[first], places[second]));
	}

	// Steps until the ship's transfer is done, or the limit; how many it took.
	std::optional<std::int64_t> UntilCarried(entt::entity ship, std::int64_t limit)
	{
		for (std::int64_t step = 1; step <= limit; ++step)
		{
			const std::size_t before = events.size();
			Advance();
			for (std::size_t index = before; index < events.size(); ++index)
				if (const auto *done = events[index].event.try_cast<Dock::CargoCarried>();
				    done && done->ship == ship)
					return step;
		}
		return std::nullopt;
	}

	std::size_t Buggies() const
	{
		return world.Registry().view<const Dock::Buggy>().size();
	}

	std::vector<std::string> Refusals()
	{
		std::vector<std::string> reasons;
		for (const Dock::PadRefused &refused : Events<Dock::PadRefused>())
			reasons.push_back(refused.reason);
		return reasons;
	}
};
} // namespace

TEST_CASE("Deck: Highport's and Corva's roads, and each pad's way from its bay")
{
	Fixture fixture;
	const Dock::RoadGraph &highport = *fixture.docks.FindRoads("ostra-highport");
	// Four bays, an apron and a hatch for each of the twelve L pads, and on each pier's road a node
	// where its bay and each of its three pads join it.
	CHECK(highport.nodes.size() == 4 + 24 + 16);
	CHECK(highport.edges.size() == 44);
	for (std::uint32_t pier = 0; pier < 4; ++pier)
	{
		const std::uint32_t first = pier * 3;
		CHECK(highport.pads[first].bay == pier);
		CHECK(highport.pads[first].length == doctest::Approx(90.6).epsilon(1.0e-3));
		CHECK(highport.pads[first + 1].length == doctest::Approx(396.8).epsilon(1.0e-3));
		CHECK(highport.pads[first + 2].length == doctest::Approx(606.8).epsilon(1.0e-3));
	}
	// Pad 1 straight from its bay along the pier's middle; pad 2 by the road down the pier's side,
	// 50 m from its middle, past pad 1.
	const auto at = [&](Ai::NodeId node) { return highport.nodes[node].position; };
	CHECK(highport.pads[0].route.size() == 3);
	CHECK(at(highport.pads[0].route[1]) == glm::dvec3{130.0, 10.0, 0.0});
	CHECK(at(highport.pads[1].route[1]) == glm::dvec3{104.0, 10.0, 50.0});
	CHECK(at(highport.pads[1].route[3]) == glm::dvec3{340.0, 10.0, 50.0});
	for (std::size_t display = 12; display < 18; ++display)
		CHECK_FALSE(highport.pads[display].reached);
	for (const auto &[from, to] : highport.edges)
		CHECK(glm::distance(at(from), at(to)) >= 2.0 * Dock::kNodeReach);

	// Corva's pier pads from the bays in its walls, and its S pads on the hub's top from the hatch
	// between them.
	const Dock::RoadGraph &corva = *fixture.docks.FindRoads("corva-refinery");
	CHECK(corva.pads[0].bay == 0);
	CHECK(corva.pads[1].bay == 0);
	CHECK(corva.pads[2].bay == 1);
	CHECK(corva.pads[3].bay == 1);
	CHECK(corva.pads[4].bay == 2);
	CHECK(corva.pads[5].bay == 2);
	CHECK(corva.pads[1].length == doctest::Approx(223.6).epsilon(1.0e-3));
	CHECK(corva.pads[4].length == doctest::Approx(53.8).epsilon(1.0e-3));
}

TEST_CASE("Deck: a layout with a pad no road reaches from a bay is refused")
{
	// A deck with an M pad, and a bay on a block beside it whose top is higher.
	Dock::StationLayout layout;
	layout.decks = {
	    {{0.0, 0.0, 0.0}, {200.0, 20.0, 200.0}}, {{150.0, 10.0, 0.0}, {100.0, 40.0, 100.0}}};
	layout.pads = {{PadClass::M, {0.0, 10.0, 0.0}}};
	layout.bays = {{{150.0, 30.0, 0.0}, {0.0, 1.0, 0.0}}};
	std::string refusal;
	try
	{
		Dock::CheckLayout("terrace", layout);
	}
	catch (const Serialise::FormatError &error)
	{
		refusal = error.what();
	}
	CHECK(refusal == "Station layout 'terrace' pad 1 is not reached by road from a cargo bay");

	// The bay on the deck's top beside the pad's stern reaches it.
	layout.bays = {{{0.0, 10.0, 80.0}, {0.0, 1.0, 0.0}}};
	CHECK_NOTHROW(Dock::CheckLayout("terrace", layout));
	CHECK(Dock::Roads(layout).pads[0].reached);

	layout.traffic.cratesPerTrip = 0;
	CHECK_THROWS_AS(Dock::CheckLayout("terrace", layout), Serialise::FormatError);
}

TEST_CASE("Deck: a shuttle's twenty crates carried aboard, the ship held until they are")
{
	Deck deck;
	const entt::entity ship = deck.Parked(1);
	deck.Step();
	REQUIRE(Dock::Carry(deck.world.Registry(), ship, 20));
	CHECK(Dock::ToCarry(deck.world.Registry(), ship) == 20);

	// Undocking is refused while its cargo crosses the deck.
	for (int step = 0; step < 600; ++step)
		deck.Advance();
	deck.world.Commands().Push(Dock::ReleasePad{{}, ship});
	deck.Advance();
	const std::vector<std::string> refused = deck.Refusals();
	REQUIRE(refused.size() == 1);
	CHECK(refused[0].starts_with("its cargo is still crossing the deck"));
	CHECK(Dock::IsDocked(deck.world.Registry(), ship, deck.station));

	const std::optional<std::int64_t> steps = deck.UntilCarried(ship, 60 * kSteps60);
	REQUIRE(steps);
	const double seconds = static_cast<double>(*steps + 601) / 60.0;
	MESSAGE("aboard in ", seconds, " s; buggies ", deck.nearest, " m apart at the nearest");
	// Five loads of four on three buggies, 117 m each way at 8 m/s: the third back out of the bay
	// arrives a round trip after the first.
	CHECK(seconds > 2.0 * 117.0 / 8.0 + 117.0 / 8.0);
	CHECK(seconds < 50.0);
	CHECK(deck.mostOut == 3);
	CHECK(deck.onDeck);
	// Passing in their lanes, two lanes apart, and otherwise further.
	CHECK(deck.nearest > 3.9);
	CHECK(Dock::ToCarry(deck.world.Registry(), ship) == 0);
	CHECK_FALSE(deck.world.Registry().all_of<Dock::Transfer>(ship));

	// Then it may go, and its buggies go back into the bay.
	deck.world.Commands().Push(Dock::ReleasePad{{}, ship});
	deck.Advance();
	CHECK(deck.Events<Dock::Left>().size() == 1);
	for (int step = 0; step < 30 * kSteps60; ++step)
		deck.Advance();
	CHECK(deck.Buggies() == 0);
}

TEST_CASE("Deck: crates carried off, the ship free once the last is taken from its hatch")
{
	Deck deck;
	const entt::entity ship = deck.Parked(2);
	deck.Step();
	REQUIRE(Dock::Carry(deck.world.Registry(), ship, -20));
	const std::optional<std::int64_t> steps = deck.UntilCarried(ship, 300 * kSteps60);
	REQUIRE(steps);
	// 423 m each way, from Highport's +X bay down the pier's road to pad 2: the last load is taken
	// from the hatch on the second round of trips, and is then still on its way to the bay.
	const double seconds = static_cast<double>(*steps) / 60.0;
	MESSAGE("off in ", seconds, " s");
	CHECK(seconds > 3.0 * 400.0 / 8.0);
	CHECK(seconds < 4.0 * 430.0 / 8.0);
	CHECK(deck.Buggies() > 0);
	CHECK(deck.onDeck);
}

TEST_CASE("Deck: bought and sold back before crossing, crates do not cross")
{
	Deck deck;
	const entt::entity ship = deck.Parked(1);
	deck.Step();
	entt::registry &registry = deck.world.Registry();
	REQUIRE(Dock::Carry(registry, ship, 10));
	REQUIRE(Dock::Carry(registry, ship, -6));
	CHECK(registry.get<Dock::Transfer>(ship) == Dock::Transfer{4, 0, 0});
	REQUIRE(Dock::Carry(registry, ship, -9));
	CHECK(registry.get<Dock::Transfer>(ship) == Dock::Transfer{0, 5, 0});

	// A ship that is not docked has nothing carried.
	const entt::entity flying = deck.Ship(deck.station, "shuttle", {0.0, 3000.0, 0.0});
	CHECK_FALSE(Dock::Carry(registry, flying, 4));
}

TEST_CASE("Deck: every pad of Highport loading at once, each pad's buggies giving way at the nodes")
{
	Deck deck;
	std::vector<entt::entity> ships;
	for (std::uint32_t number = 1; number <= 12; ++number)
		ships.push_back(deck.Parked(number, "heavy-transport"));
	deck.Step();
	for (std::size_t index = 0; index < ships.size(); ++index)
		REQUIRE(Dock::Carry(deck.world.Registry(), ships[index], index % 2 == 0 ? 40 : -40));
	std::map<entt::entity, std::int64_t> done;
	for (std::int64_t step = 1; step <= 600 * kSteps60 && done.size() < ships.size(); ++step)
	{
		const std::size_t before = deck.events.size();
		deck.Advance();
		for (std::size_t index = before; index < deck.events.size(); ++index)
			if (const auto *carried = deck.events[index].event.try_cast<Dock::CargoCarried>())
				done.emplace(carried->ship, step);
	}
	REQUIRE(done.size() == ships.size());
	std::int64_t last = 0;
	for (const auto &[ship, step] : done)
		last = std::max(last, step);
	MESSAGE(
	    "all twelve carried in ", static_cast<double>(last) / 60.0, " s; buggies ", deck.nearest,
	    " m apart at the nearest");
	CHECK(deck.mostOut == 3);
	CHECK(deck.onDeck);
	CHECK(deck.nearest > 3.9);
}

TEST_CASE("Deck: parked buggies, the cargo moved at the rate working ones would move it")
{
	Deck deck{"highport-parked"};
	const entt::entity ship = deck.Parked(1);
	deck.Step();
	REQUIRE(Dock::Carry(deck.world.Registry(), ship, 20));
	const std::optional<std::int64_t> steps = deck.UntilCarried(ship, 120 * kSteps60);
	REQUIRE(steps);
	// A load each 585 steps, the round trip of 2 x 116.98 m at 8 m/s shared by three buggies, and
	// five loads.
	CHECK(*steps == 5 * 585);
	CHECK(deck.buggySteps == 0);
}

TEST_CASE("Deck: a station with no bays has its cargo aboard at once")
{
	Fixture fixture{false};
	fixture.docks.DefineHull("skiff", {"Skiff", 9.8f, 6.4f, 1.8f, PadClass::S, {}});
	Dock::StationLayout slab;
	slab.decks = {{{0.0, 0.0, 0.0}, {200.0, 20.0, 200.0}}};
	slab.pads = {{PadClass::S, {0.0, 10.0, 0.0}}};
	fixture.docks.DefineLayout("slab", slab);
	const entt::entity station =
	    fixture.docks.MakeStation(fixture.world, DockTests::kHalcyon, DockTests::kHighport, "slab");
	const entt::entity ship = fixture.Ship(station, "skiff", {0.0, 3000.0, 0.0});
	fixture.docks.Park(fixture.world, ship, fixture.PadNumbered(station, 1));
	REQUIRE(Dock::Carry(fixture.world.Registry(), ship, 20));
	fixture.Step();
	CHECK(fixture.Events<Dock::CargoCarried>().size() == 1);
	CHECK(Dock::ToCarry(fixture.world.Registry(), ship) == 0);
}

TEST_CASE("Deck: a ship gone mid-transfer, its buggies bring back what they carry and go in")
{
	Deck deck;
	const entt::entity ship = deck.Parked(1);
	deck.Step();
	REQUIRE(Dock::Carry(deck.world.Registry(), ship, 20));
	for (int step = 0; step < 5 * kSteps60; ++step)
		deck.Advance();
	REQUIRE(deck.Buggies() == 3);
	deck.world.Registry().destroy(ship);
	for (int step = 0; step < 30 * kSteps60; ++step)
		deck.Advance();
	CHECK(deck.Buggies() == 0);
	CHECK(deck.Events<Dock::CargoCarried>().empty());
}

TEST_CASE(
    "Deck: the same transfer from the same start, and from a save part-way, reaches the same state")
{
	constexpr int kSaveAt = 20 * 60;
	constexpr int kSteps = 50 * 60;
	const auto run = [](std::optional<std::vector<std::byte>> &saved, int saveAt)
	{
		Deck deck;
		const entt::entity first = deck.Parked(1);
		const entt::entity second = deck.Parked(2, "heavy-transport");
		deck.Step();
		Dock::Carry(deck.world.Registry(), first, 20);
		Dock::Carry(deck.world.Registry(), second, -30);
		for (int step = 0; step < kSteps; ++step)
		{
			if (step == saveAt)
			{
				CHECK(deck.Buggies() == 6);
				saved = deck.world.Save();
			}
			deck.Step();
		}
		return deck.world.StateHash();
	};
	std::optional<std::vector<std::byte>> save;
	std::optional<std::vector<std::byte>> unused;
	const std::uint64_t first = run(save, kSaveAt);
	CHECK(run(unused, -1) == first);
	// Recorded from a Debug build and checked by the Release suite and under the sanitisers too, as
	// the approach's is: a change here is a change to how buggies drive, or to what a save holds.
	CHECK(first == 0x606d82c25155ac52ull);

	Deck loaded;
	loaded.world.Load(*save);
	for (int step = kSaveAt; step < kSteps; ++step)
		loaded.Step();
	CHECK(loaded.world.StateHash() == first);
}
