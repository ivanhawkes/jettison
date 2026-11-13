#include "Fixture.h"

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <format>
#include <string>
#include <utility>
#include <vector>

using namespace Jettison;
using Dock::PadClass;
using DockTests::Fixture;

namespace
{
// A yard of every kind of pad on one deck 400 m square, on Highport's rails: 1 M, 2 S, 3 L, 4 an S
// for display, 5 M. A reservation is held two seconds.
Dock::StationLayout YardLayout()
{
	Dock::StationLayout yard;
	yard.displayName = "Yard";
	yard.reservationSeconds = 2.0f;
	yard.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	yard.pads = {
	    {PadClass::M, {-100.0, 10.0, -100.0}},
	    {PadClass::S, {100.0, 10.0, -100.0}},
	    {PadClass::L, {0.0, 10.0, 100.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}},
	    {PadClass::S, {100.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}, true},
	    {PadClass::M, {-100.0, 10.0, 0.0}},
	};
	return yard;
}

struct Yard : Fixture
{
	entt::entity station{entt::null};
	// Ships wait 2 km above the yard, apart, where nothing flies them.
	int ships{0};

	Yard() : Fixture{false}
	{
		docks.DefineHull("skiff", {"Skiff", 9.8f, 6.4f, 1.8f, PadClass::S, {}});
		docks.DefineHull("hauler", {"Hauler", 50.0f, 25.0f, 12.0f, PadClass::M, {}});
		docks.DefineHull("barge", {"Barge", 130.0f, 45.0f, 25.0f, PadClass::L, {}});
		docks.DefineHull("liner", {"Liner", 280.0f, 70.0f, 50.0f, PadClass::XL, {}});
		docks.DefineLayout("yard", YardLayout());
		station = docks.MakeStation(world, DockTests::kHalcyon, DockTests::kHighport, "yard");
	}

	entt::entity Waiting(std::string_view hull)
	{
		return Ship(station, hull, {300.0 * ships++, 2000.0, 0.0});
	}

	void Ask(entt::entity ship, Sim::Controller sender = {})
	{
		world.Commands().Push(Dock::RequestPad{sender, ship, station, false});
	}

	// The pads given, in the order they were, as ship and number.
	std::vector<std::pair<entt::entity, std::uint32_t>> Given() const
	{
		std::vector<std::pair<entt::entity, std::uint32_t>> given;
		for (const Dock::PadGiven &event : Events<Dock::PadGiven>())
			given.emplace_back(event.ship, event.number);
		return given;
	}

	std::vector<std::string> Refusals() const
	{
		std::vector<std::string> reasons;
		for (const Dock::PadRefused &event : Events<Dock::PadRefused>())
			reasons.push_back(Sim::Describe(event.reason));
		return reasons;
	}

	// The ship put where its hull rests on the pad, turned to it, at rest.
	void PutOn(entt::entity ship, std::uint32_t number, double height)
	{
		entt::registry &registry = world.Registry();
		const Sim::Transform &pad = registry.get<Sim::Transform>(PadNumbered(station, number));
		registry.replace<Sim::Transform>(
		    ship,
		    Sim::Transform{
		        pad.position + pad.orientation * glm::dvec3{0.0, height * 0.5, 0.0}, pad.orientation});
		registry.replace<Physics::Velocity>(ship);
	}
};
} // namespace

TEST_CASE("Docks: two ships asking in one step are given pads in command order, the same every run")
{
	const auto ask = [](bool reversed)
	{
		Yard yard;
		const entt::entity first = yard.Waiting("skiff");
		const entt::entity second = yard.Waiting("skiff");
		yard.Ask(reversed ? second : first);
		yard.Ask(reversed ? first : second);
		yard.Step();
		return std::pair{yard.Given(), std::pair{first, second}};
	};
	// The smallest free pad a skiff fits is pad 2, the S; then the M pads, lowest numbered first.
	const auto [given, ships] = ask(false);
	CHECK(
	    given ==
	    std::vector<std::pair<entt::entity, std::uint32_t>>{{ships.first, 2}, {ships.second, 1}});
	CHECK(ask(false).first == given);
	const auto [reversed, same] = ask(true);
	CHECK(
	    reversed ==
	    std::vector<std::pair<entt::entity, std::uint32_t>>{{same.second, 2}, {same.first, 1}});
}

TEST_CASE("Docks: a hull that fits only an L pad is never given an M one, and waits for the L")
{
	Yard yard;
	const entt::entity barge = yard.Waiting("barge");
	const entt::entity second = yard.Waiting("barge");
	yard.Ask(barge);
	yard.Ask(second);
	yard.Step();
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{barge, 3}});
	const std::vector<Dock::PadQueued> queued = yard.Events<Dock::PadQueued>();
	REQUIRE(queued.size() == 1);
	CHECK(queued[0].ship == second);
	CHECK(queued[0].place == 1);
	// Both M pads are still free.
	CHECK(yard.PadOf(yard.PadNumbered(yard.station, 1)).state == Dock::PadState::Free);
	CHECK(yard.PadOf(yard.PadNumbered(yard.station, 5)).state == Dock::PadState::Free);
}

TEST_CASE("Docks: a full station's queue is served in order as pads come free")
{
	Yard yard;
	const entt::entity first = yard.Waiting("hauler");
	const entt::entity second = yard.Waiting("hauler");
	const entt::entity barge = yard.Waiting("barge");
	const entt::entity waiting = yard.Waiting("hauler");
	const entt::entity skiff = yard.Waiting("skiff");
	const entt::entity small = yard.Waiting("skiff");
	const entt::entity last = yard.Waiting("hauler");
	for (const entt::entity ship : {first, second, barge, waiting, skiff, small, last})
		yard.Ask(ship);
	yard.Step();
	// The skiff behind the waiting hauler takes the S pad, which the hauler does not fit; the next
	// skiff finds nothing, the display pad being no one's.
	CHECK(
	    yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{
	                        {first, 1}, {second, 5}, {barge, 3}, {skiff, 2}});
	REQUIRE(
	    yard.world.Registry().get<Dock::Station>(yard.station).queue ==
	    std::vector<entt::entity>{waiting, small, last});

	// Pads come free one at a time, and each goes to the first in the queue it fits.
	yard.events.clear();
	yard.world.Commands().Push(Dock::ReleasePad{{}, barge});
	yard.Step();
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{waiting, 3}});
	yard.events.clear();
	yard.world.Commands().Push(Dock::ReleasePad{{}, skiff});
	yard.world.Commands().Push(Dock::ReleasePad{{}, second});
	yard.Step();
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{small, 2}, {last, 5}});
	CHECK(yard.world.Registry().get<Dock::Station>(yard.station).queue.empty());
	CHECK(yard.Events<Dock::PadReleased>().size() == 2);
}

TEST_CASE("Docks: a reservation not landed on in time lapses, and the pad goes to the next in line")
{
	Yard yard;
	// The M and L pads held by ships parked on them, which hold them for good.
	for (const auto &[hull, number] :
	     std::vector<std::pair<std::string, std::uint32_t>>{{"hauler", 1}, {"hauler", 5}, {"barge", 3}})
		yard.docks.Park(yard.world, yard.Waiting(hull), yard.PadNumbered(yard.station, number));
	const entt::entity late = yard.Waiting("skiff");
	const entt::entity next = yard.Waiting("skiff");
	yard.Ask(late);
	yard.Ask(next);
	yard.Step();
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{late, 2}});

	// Two seconds, 120 steps from the one it was given in.
	yard.Run(2.0 - DockTests::kStep * 1.5);
	CHECK(yard.Events<Dock::PadLapsed>().empty());
	yard.Run(DockTests::kStep * 2.0);
	const std::vector<Dock::PadLapsed> lapsed = yard.Events<Dock::PadLapsed>();
	REQUIRE(lapsed.size() == 1);
	CHECK(lapsed[0].ship == late);
	CHECK(lapsed[0].number == 2);
	CHECK_FALSE(yard.world.Registry().all_of<Dock::Docking>(late));
	CHECK(yard.Given().back() == std::pair{next, std::uint32_t{2}});
}

TEST_CASE("Docks: a display pad is never given, and a ship parked holds its pad")
{
	Yard yard;
	for (const auto &[hull, number] : std::vector<std::pair<std::string, std::uint32_t>>{
	         {"hauler", 1}, {"skiff", 2}, {"barge", 3}, {"hauler", 5}})
		yard.docks.Park(yard.world, yard.Waiting(hull), yard.PadNumbered(yard.station, number));
	CHECK_THROWS_AS(
	    yard.docks.Park(yard.world, yard.Waiting("skiff"), yard.PadNumbered(yard.station, 4)),
	    std::invalid_argument);
	const entt::entity skiff = yard.Waiting("skiff");
	yard.Ask(skiff);
	yard.Run(1.0);
	CHECK(yard.Given().empty());
	CHECK(yard.Events<Dock::PadQueued>().size() == 1);
	CHECK(yard.PadOf(yard.PadNumbered(yard.station, 4)).state == Dock::PadState::Free);
	// The parked ships are clamped where they rest, out of flight.
	const entt::entity parked = yard.PadOf(yard.PadNumbered(yard.station, 2)).ship;
	CHECK(Dock::IsDocked(yard.world.Registry(), parked, yard.station));
	CHECK(yard.world.Registry().get<Physics::Body>(parked).kind == Physics::BodyKind::Kinematic);
}

TEST_CASE(
    "Docks: a request is refused, saying why, for a ship its sender does not control or cannot dock")
{
	Yard yard;
	entt::registry &registry = yard.world.Registry();
	const entt::entity ship = yard.Waiting("skiff");
	const Sim::Controller one{Sim::ControllerKind::Player, 1};
	const Sim::Controller two{Sim::ControllerKind::Player, 2};
	registry.emplace<Sim::ControlledBy>(ship, one);
	yard.Ask(ship, two);
	yard.world.Commands().Push(Dock::ReleasePad{{}, ship});
	yard.Ask(ship, one);
	yard.Ask(ship, one);
	yard.world.Commands().Push(Dock::RequestPad{{}, ship, ship, false});
	yard.Ask(yard.Waiting("liner"));
	const entt::entity away = yard.Waiting("skiff");
	registry.replace<Sim::InSystem>(away, Sim::SystemId("Vesper"));
	registry.remove<Physics::LocalFrame>(away);
	yard.Ask(away);
	yard.world.Commands().Push(Dock::ReleasePad{two, ship});
	yard.Step();
	const auto station = std::format("entity {:#x}", entt::to_integral(yard.station));
	CHECK(
	    yard.Refusals() ==
	    std::vector<std::string>{
	        "reason.dock.not_controller()", "reason.dock.holds_nothing()",
	        std::format("reason.dock.already_holds_pad(2, {})", station), "reason.dock.not_a_station()",
	        std::format("reason.dock.no_pad_fits({})", station),
	        std::format("reason.dock.other_system({})", station), "reason.dock.not_controller()"});
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{ship, 2}});
}

TEST_CASE("Docks: a pad clamps only the ship it was given to")
{
	Yard yard;
	const entt::entity given = yard.Waiting("skiff");
	const entt::entity other = yard.Waiting("skiff");
	yard.Ask(given);
	yard.Ask(other);
	yard.Step();
	REQUIRE(yard.NumberOf(given) == 2);
	REQUIRE(yard.NumberOf(other) == 1);

	// The other ship set down on the given one's pad, at rest, turned to it: not clamped.
	yard.PutOn(other, 2, 1.8);
	yard.Run(0.5);
	CHECK(yard.Events<Dock::Landed>().empty());
	CHECK(yard.world.Registry().get<Physics::Body>(other).kind == Physics::BodyKind::Dynamic);

	// Its own ship is.
	yard.PutOn(other, 1, 1.8);
	yard.PutOn(given, 2, 1.8);
	yard.Step();
	const std::vector<Dock::Landed> landed = yard.Events<Dock::Landed>();
	REQUIRE(landed.size() == 2);
	CHECK(landed[0].ship == given);
	CHECK(landed[0].number == 2);
	CHECK(Dock::IsDocked(yard.world.Registry(), given, yard.station));
	CHECK(yard.PadOf(yard.PadNumbered(yard.station, 2)).state == Dock::PadState::Holding);
}
