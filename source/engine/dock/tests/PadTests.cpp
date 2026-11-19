#include "Fixture.h"

#include <jettison/space/Cruise.h>
#include <jettison/space/Lanes.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>

#include <format>
#include <string>
#include <utility>
#include <vector>

using namespace Jettison;
using DockTests::Fixture;

namespace
{
// A yard of every kind of pad on one deck 400 m square, on Highport's rails: 1 frigate, 2 corvette, 3
// destroyer, 4 a corvette for display, 5 frigate. A reservation is held two seconds.
Dock::StationLayout YardLayout()
{
	Dock::StationLayout yard;
	yard.displayName = "Yard";
	yard.reservationSeconds = 2.0f;
	yard.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	yard.pads = {
	    {"frigate", {-100.0, 10.0, -100.0}},
	    {"corvette", {100.0, 10.0, -100.0}},
	    {"destroyer", {0.0, 10.0, 100.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}},
	    {"corvette", {100.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}, true},
	    {"frigate", {-100.0, 10.0, 0.0}},
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
		for (const auto &[name, padClass] : DockTests::SampleClasses())
			docks.DefinePadClass(name, padClass);
		docks.DefineHull("skiff", {"Skiff", 9.8f, 6.4f, 1.8f, "corvette", {}});
		docks.DefineHull("hauler", {"Hauler", 50.0f, 25.0f, 12.0f, "frigate", {}});
		docks.DefineHull("barge", {"Barge", 130.0f, 45.0f, 25.0f, "destroyer", {}});
		docks.DefineHull("liner", {"Liner", 280.0f, 70.0f, 50.0f, "cruiser", {}});
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
	// The smallest free pad a skiff fits is pad 2, the corvette; then the frigate pads, lowest numbered
	// first.
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


TEST_CASE(
    "Docks: a hull that fits only a destroyer pad is never given a frigate one, and waits for the "
    "destroyer")
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
	// Both frigate pads are still free.
	CHECK(yard.PadOf(yard.PadNumbered(yard.station, 1)).state == Dock::PadState::Free);
	CHECK(yard.PadOf(yard.PadNumbered(yard.station, 5)).state == Dock::PadState::Free);
}


TEST_CASE(
    "Docks: with space's lanes and cruise attached, nothing orders them against the docks by chance")
{
	// The lanes attached after the docks, as Manifest's host does, and before, as the sample does.
	for (const bool lanesFirst : {false, true})
	{
		Sim::SimWorld world{DockTests::kStep, 9};
		Physics::PhysicsWorld &physics{Fixture::Setup(world)};
		Ai::Steering::Attach(world, physics, Spatial::SystemIndices::Attach(world));
		if (lanesFirst)
		{
			Space::LaneTravel::Attach(world, physics);
			Space::CruiseTravel::Attach(world, physics);
		}
		Dock::Docks::Attach(world, physics);
		if (!lanesFirst)
		{
			Space::LaneTravel::Attach(world, physics);
			Space::CruiseTravel::Attach(world, physics);
		}
		const Ecs::Scheduler &scheduler = world.Scheduler();
		CHECK(scheduler.Ambiguities(Ecs::TickPhase::Simulation).empty());
		std::vector<std::string> order = scheduler.Order(Ecs::TickPhase::Simulation);
		std::erase_if(
		    order, [](const std::string &name)
		    { return name != "dock" && name != "lanes" && name != "cruise"; });
		CHECK(order == std::vector<std::string>{"dock", "lanes", "cruise"});
	}
}


TEST_CASE("Docks: a game's own pad classes, a compact berth below a bay, are given by their ranks")
{
	DockTests::Fixture fixture{false};
	Dock::Docks &docks = fixture.docks;
	docks.DefinePadClass("berth", {0, {6.0f, 4.0f, 3.0f}, {8.0f, 10.0f}});
	docks.DefinePadClass("bay", {1, {25.0f, 20.0f, 8.0f}, {35.0f, 40.0f}});
	CHECK_THROWS_AS(
	    docks.DefinePadClass("bay", {0, {6.0f, 4.0f, 3.0f}, {8.0f, 10.0f}}), std::invalid_argument);
	docks.DefineHull("pod", {"Pod", 5.0f, 3.0f, 2.0f, "berth", {}});
	docks.DefineHull("tug", {"Tug", 18.0f, 12.0f, 5.0f, "bay", {}});
	CHECK(docks.Fits(*docks.FindHull("pod"), "bay"));
	CHECK_FALSE(docks.Fits(*docks.FindHull("tug"), "berth"));
	CHECK_FALSE(docks.Fits(*docks.FindHull("pod"), "hangar"));

	// Pad 1 a bay and pad 2 a berth; a pod takes the berth, the smaller, and a second tug waits for
	// the bay rather than take it.
	Dock::StationLayout dock;
	dock.displayName = "Dock";
	dock.decks = {{{0.0, 0.0, 0.0}, {100.0, 20.0, 100.0}}};
	dock.pads = {{"bay", {-20.0, 10.0, 0.0}}, {"berth", {20.0, 10.0, 0.0}}};
	docks.DefineLayout("dock", dock);
	const entt::entity station =
	    docks.MakeStation(fixture.world, DockTests::kHalcyon, DockTests::kHighport, "dock");
	const entt::entity pod = fixture.Ship(station, "pod", {0.0, 2000.0, 0.0});
	const entt::entity tug = fixture.Ship(station, "tug", {300.0, 2000.0, 0.0});
	const entt::entity second = fixture.Ship(station, "tug", {600.0, 2000.0, 0.0});
	for (const entt::entity ship : {pod, tug, second})
		fixture.world.Commands().Push(Dock::RequestPad{{}, ship, station, false});
	fixture.Step();
	CHECK(fixture.NumberOf(pod) == 2);
	CHECK(fixture.NumberOf(tug) == 1);
	CHECK(fixture.NumberOf(second) == 0);
	CHECK(fixture.Events<Dock::PadQueued>().size() == 1);
}


TEST_CASE("Docks: a class ranked above or level with a larger one is not given its hulls")
{
	DockTests::Fixture fixture{false};
	Dock::Docks &docks = fixture.docks;
	// Each is a class by itself; the ranks are the game's, and these do not follow the sizes.
	docks.DefinePadClass("long", {0, {140.0f, 50.0f, 30.0f}, {80.0f, 180.0f}});
	docks.DefinePadClass("short", {1, {25.0f, 20.0f, 8.0f}, {35.0f, 40.0f}});
	docks.DefinePadClass("level", {0, {25.0f, 20.0f, 8.0f}, {35.0f, 40.0f}});
	docks.DefineHull("barge", {"Barge", 130.0f, 45.0f, 25.0f, "long", {}});
	docks.DefineHull("pod", {"Pod", 18.0f, 12.0f, 5.0f, "level", {}});
	CHECK_FALSE(docks.Fits(*docks.FindHull("barge"), "short"));
	CHECK_FALSE(docks.Fits(*docks.FindHull("barge"), "level"));
	CHECK(docks.Fits(*docks.FindHull("barge"), "long"));
	// Level in rank and within its largest hull: a pod lands on a long pad, and on a short one.
	CHECK(docks.Fits(*docks.FindHull("pod"), "long"));
	CHECK(docks.Fits(*docks.FindHull("pod"), "short"));

	Dock::StationLayout dock;
	dock.displayName = "Dock";
	dock.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	dock.pads = {{"short", {0.0, 10.0, 0.0}}};
	docks.DefineLayout("dock", dock);
	const entt::entity station =
	    docks.MakeStation(fixture.world, DockTests::kHalcyon, DockTests::kHighport, "dock");
	const entt::entity barge = fixture.Ship(station, "barge", {0.0, 2000.0, 0.0});
	fixture.world.Commands().Push(Dock::RequestPad{{}, barge, station, false});
	fixture.Step();
	CHECK(fixture.NumberOf(barge) == 0);
	REQUIRE(fixture.Events<Dock::PadRefused>().size() == 1);
	CHECK(fixture.Events<Dock::PadRefused>()[0].reason.key == "reason.dock.no_pad_fits");
}


TEST_CASE("Docks: a ship asking for a feature is given only a pad that offers it, and queues for one")
{
	DockTests::Fixture fixture{false};
	Dock::Docks &docks = fixture.docks;
	for (const auto &[name, padClass] : DockTests::SampleClasses())
		docks.DefinePadClass(name, padClass);
	docks.DefineHull("skiff", {"Skiff", 9.8f, 6.4f, 1.8f, "corvette", {}});
	// Pad 1 a plain corvette's, pad 2 a frigate's that offers a dry dock, pad 3 one for display that
	// says it does, which no ship is given.
	Dock::StationLayout yard;
	yard.displayName = "Yard";
	yard.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	yard.pads = {
	    {"corvette", {-100.0, 10.0, 0.0}},
	    {"frigate", {0.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}, false, {}, {"dry-dock"}},
	    {"corvette", {100.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}, true, {}, {"dry-dock"}},
	};
	docks.DefineLayout("yard", yard);
	const entt::entity station =
	    docks.MakeStation(fixture.world, DockTests::kHalcyon, DockTests::kHighport, "yard");
	const entt::registry &registry = fixture.world.Registry();
	CHECK_FALSE(docks.Offers(registry, fixture.PadNumbered(station, 1), "dry-dock"));
	CHECK(docks.Offers(registry, fixture.PadNumbered(station, 2), "dry-dock"));
	CHECK_FALSE(docks.Offers(registry, fixture.PadNumbered(station, 2), "paint"));
	CHECK_FALSE(docks.Offers(registry, station, "dry-dock"));

	// The first is given the dry dock over the smaller free pad, which the second, asking for none,
	// takes; the third waits for the dry dock; the fourth asks for what no pad offers.
	const entt::entity first = fixture.Ship(station, "skiff", {0.0, 2000.0, 0.0});
	const entt::entity second = fixture.Ship(station, "skiff", {300.0, 2000.0, 0.0});
	const entt::entity third = fixture.Ship(station, "skiff", {600.0, 2000.0, 0.0});
	const entt::entity fourth = fixture.Ship(station, "skiff", {900.0, 2000.0, 0.0});
	fixture.world.Commands().Push(Dock::RequestPad{{}, first, station, false, "dry-dock"});
	fixture.world.Commands().Push(Dock::RequestPad{{}, second, station, false});
	fixture.world.Commands().Push(Dock::RequestPad{{}, third, station, false, "dry-dock"});
	fixture.world.Commands().Push(Dock::RequestPad{{}, fourth, station, false, "paint"});
	fixture.Step();
	CHECK(fixture.NumberOf(first) == 2);
	CHECK(fixture.NumberOf(second) == 1);
	CHECK(fixture.NumberOf(third) == 0);
	CHECK(registry.get<Dock::Docking>(third).stage == Dock::Stage::Queued);
	CHECK_FALSE(registry.all_of<Dock::Docking>(fourth));
	REQUIRE(fixture.Events<Dock::PadRefused>().size() == 1);
	CHECK(fixture.Events<Dock::PadRefused>()[0].ship == fourth);
	CHECK(fixture.Events<Dock::PadRefused>()[0].reason.key == "reason.dock.no_pad_offers");

	// The plain pad coming free is not the third's; the dry dock is.
	fixture.world.Commands().Push(Dock::ReleasePad{{}, second});
	fixture.Step();
	CHECK(fixture.NumberOf(third) == 0);
	fixture.world.Commands().Push(Dock::ReleasePad{{}, first});
	fixture.Step();
	CHECK(fixture.NumberOf(third) == 2);

	CHECK(registry.get<Dock::Docking>(third).feature == "dry-dock");
}


TEST_CASE("Dock definitions: a layout's pad keeps its features through its file")
{
	Dock::RegisterTypes();
	Dock::StationLayout layout;
	layout.decks = {{{0.0, 0.0, 0.0}, {200.0, 20.0, 200.0}}};
	layout.pads = {
	    {"frigate",
	     {0.0, 10.0, 0.0},
	     {0.0, 1.0, 0.0},
	     {0.0, 0.0, -1.0},
	     false,
	     {},
	     {"dry-dock", "fuel"}},
	    {"frigate", {0.0, 10.0, 90.0}}};
	const auto read = Serialise::YamlArchive::Read<Dock::StationLayout>(
	    Serialise::YamlArchive::Write(layout, {Dock::kDefinitionVersion}), {Dock::kDefinitionVersion});
	REQUIRE(read.pads.size() == 2);
	CHECK(read.pads[0].features == std::vector<std::string>{"dry-dock", "fuel"});
	CHECK(read.pads[1].features.empty());
	// A file that leaves the field out, as every one written before it does, reads as offering none.
	const auto bare = Serialise::YamlArchive::Read<Dock::StationLayout>(
	    "version: 1\npads:\n  - {class: frigate, position: [0, 10, 0]}\n", {Dock::kDefinitionVersion});
	REQUIRE(bare.pads.size() == 1);
	CHECK(bare.pads[0].features.empty());
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
	// The skiff behind the waiting hauler takes the corvette pad, which the hauler does not fit; the
	// next skiff finds nothing, the display pad being no one's.
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
	// The frigate and destroyer pads held by ships parked on them, which hold them for good.
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
	registry.replace<Sim::InFrame>(away, Sim::FrameId("Vesper"));
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


namespace
{
// A pad as version 1 had it, its class an enumerator of the closed four.
enum class PadClassV1 : std::uint8_t
{
	S,
	M,
	L,
	XL
};

struct PadV1
{
	entt::entity station{entt::null};
	std::uint32_t number{0};
	PadClassV1 padClass{PadClassV1::S};
	bool display{false};
	Dock::PadState state{Dock::PadState::Free};
	entt::entity ship{entt::null};
	std::uint64_t lapses{0};
};
} // namespace


TEST_CASE("Docks: a pad saved with its class an enumerator loads with the class's name")
{
	using Reflect::Attribute;
	Dock::RegisterTypes();
	Reflect::RegisterEnum<PadClassV1>("DockTests.PadClassV1")
	    .Value<PadClassV1::S>("S")
	    .Value<PadClassV1::M>("M")
	    .Value<PadClassV1::L>("L")
	    .Value<PadClassV1::XL>("XL");
	Reflect::RegisterType<PadV1>("DockTests.PadV1")
	    .Field<&PadV1::station>("station", Attribute::Saved)
	    .Field<&PadV1::number>("number", Attribute::Saved)
	    .Field<&PadV1::padClass>("padClass", Attribute::Saved)
	    .Field<&PadV1::display>("display", Attribute::Saved)
	    .Field<&PadV1::state>("state", Attribute::Saved)
	    .Field<&PadV1::ship>("ship", Attribute::Saved)
	    .Field<&PadV1::lapses>("lapses", Attribute::Saved);

	Sim::SimWorld old{DockTests::kStep, 1};
	old.RegisterComponent<PadV1>();
	const entt::entity station = old.Registry().create();
	old.Registry().emplace<PadV1>(
	    old.Registry().create(),
	    PadV1{station, 3, PadClassV1::XL, true, Dock::PadState::Holding, station, 42});
	Serialise::BinaryDocument document = Serialise::BinaryDocument::Parse(old.Save(), "Jettison.World");
	document.Rename("DockTests.PadV1", "Dock.Pad");

	Sim::SimWorld world{DockTests::kStep, 1};
	world.RegisterComponent<Dock::Pad>();
	world.Load(document.Emit());
	CHECK(
	    world.Registry().get<Dock::Pad>(entt::entity{1}) ==
	    Dock::Pad{station, 3, "XL", true, Dock::PadState::Holding, station, 42});
}
