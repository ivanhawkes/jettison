#include "Fixture.h"

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
// A yard of every kind of berth on one deck 400 m square, on the ferry's rails: 1 frigate, 2 corvette,
// 3 destroyer, 4 a corvette for display, 5 frigate. A reservation is held two seconds.
Dock::PlatformLayout YardLayout()
{
	Dock::PlatformLayout yard;
	yard.displayName = "Yard";
	yard.reservationSeconds = 2.0f;
	yard.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	yard.berths = {
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
	entt::entity platform{entt::null};
	// Vehicles wait 2 km above the yard, apart, where nothing flies them.
	int vehicles{0};


	Yard() : Fixture{false}
	{
		for (const auto &[name, berthClass] : DockTests::SampleClasses())
			docks.DefineBerthClass(name, berthClass);
		docks.DefineVehicle("skiff", {"Skiff", 9.8f, 6.4f, 1.8f, "corvette", {}});
		docks.DefineVehicle("hauler", {"Hauler", 50.0f, 25.0f, 12.0f, "frigate", {}});
		docks.DefineVehicle("barge", {"Barge", 130.0f, 45.0f, 25.0f, "destroyer", {}});
		docks.DefineVehicle("liner", {"Liner", 280.0f, 70.0f, 50.0f, "cruiser", {}});
		docks.DefineLayout("yard", YardLayout());
		platform = docks.MakePlatform(world, DockTests::kHarbour, DockTests::kFerry, "yard");
	}


	entt::entity Waiting(std::string_view vehicle)
	{
		return Vehicle(platform, vehicle, {300.0 * vehicles++, 2000.0, 0.0});
	}


	void Ask(entt::entity vehicle, Sim::Controller sender = {})
	{
		world.Commands().Push(Dock::RequestBerth{sender, vehicle, platform, false});
	}


	// The berths given, in the order they were, as vehicle and number.
	std::vector<std::pair<entt::entity, std::uint32_t>> Given() const
	{
		std::vector<std::pair<entt::entity, std::uint32_t>> given;
		for (const Dock::BerthGiven &event : Events<Dock::BerthGiven>())
			given.emplace_back(event.vehicle, event.number);
		return given;
	}


	std::vector<std::string> Refusals() const
	{
		std::vector<std::string> reasons;
		for (const Dock::BerthRefused &event : Events<Dock::BerthRefused>())
			reasons.push_back(Sim::Describe(event.reason));
		return reasons;
	}


	// The vehicle put where its vehicle rests on the berth, turned to it, at rest.
	void PutOn(entt::entity vehicle, std::uint32_t number, double height)
	{
		entt::registry &registry = world.Registry();
		const Sim::Transform &berth = registry.get<Sim::Transform>(BerthNumbered(platform, number));
		registry.replace<Sim::Transform>(
		    vehicle, Sim::Transform{
		                 berth.position + berth.orientation * glm::dvec3{0.0, height * 0.5, 0.0},
		                 berth.orientation});
		registry.replace<Physics::Velocity>(vehicle);
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
	// The smallest free berth a skiff fits is berth 2, the corvette; then the frigate berths, lowest
	// numbered first.
	const auto [given, vehicles] = ask(false);
	CHECK(
	    given ==
	    std::vector<std::pair<entt::entity, std::uint32_t>>{{vehicles.first, 2}, {vehicles.second, 1}});
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
	const std::vector<Dock::BerthQueued> queued = yard.Events<Dock::BerthQueued>();
	REQUIRE(queued.size() == 1);
	CHECK(queued[0].vehicle == second);
	CHECK(queued[0].place == 1);
	// Both frigate berths are still free.
	CHECK(yard.BerthOf(yard.BerthNumbered(yard.platform, 1)).state == Dock::BerthState::Free);
	CHECK(yard.BerthOf(yard.BerthNumbered(yard.platform, 5)).state == Dock::BerthState::Free);
}


TEST_CASE("Docks: a game's own pad classes, a compact berth below a bay, are given by their ranks")
{
	DockTests::Fixture fixture{false};
	Dock::Docks &docks = fixture.docks;
	docks.DefineBerthClass("berth", {0, {6.0f, 4.0f, 3.0f}, {8.0f, 10.0f}});
	docks.DefineBerthClass("bay", {1, {25.0f, 20.0f, 8.0f}, {35.0f, 40.0f}});
	CHECK_THROWS_AS(
	    docks.DefineBerthClass("bay", {0, {6.0f, 4.0f, 3.0f}, {8.0f, 10.0f}}), std::invalid_argument);
	docks.DefineVehicle("pod", {"Pod", 5.0f, 3.0f, 2.0f, "berth", {}});
	docks.DefineVehicle("tug", {"Tug", 18.0f, 12.0f, 5.0f, "bay", {}});
	CHECK(docks.Fits(*docks.FindVehicle("pod"), "bay"));
	CHECK_FALSE(docks.Fits(*docks.FindVehicle("tug"), "berth"));
	CHECK_FALSE(docks.Fits(*docks.FindVehicle("pod"), "hangar"));

	// Berth 1 a bay and berth 2 a berth; a pod takes the berth, the smaller, and a second tug waits for
	// the bay rather than take it.
	Dock::PlatformLayout dock;
	dock.displayName = "Dock";
	dock.decks = {{{0.0, 0.0, 0.0}, {100.0, 20.0, 100.0}}};
	dock.berths = {{"bay", {-20.0, 10.0, 0.0}}, {"berth", {20.0, 10.0, 0.0}}};
	docks.DefineLayout("dock", dock);
	const entt::entity platform =
	    docks.MakePlatform(fixture.world, DockTests::kHarbour, DockTests::kFerry, "dock");
	const entt::entity pod = fixture.Vehicle(platform, "pod", {0.0, 2000.0, 0.0});
	const entt::entity tug = fixture.Vehicle(platform, "tug", {300.0, 2000.0, 0.0});
	const entt::entity second = fixture.Vehicle(platform, "tug", {600.0, 2000.0, 0.0});
	for (const entt::entity vehicle : {pod, tug, second})
		fixture.world.Commands().Push(Dock::RequestBerth{{}, vehicle, platform, false});
	fixture.Step();
	CHECK(fixture.NumberOf(pod) == 2);
	CHECK(fixture.NumberOf(tug) == 1);
	CHECK(fixture.NumberOf(second) == 0);
	CHECK(fixture.Events<Dock::BerthQueued>().size() == 1);
}


TEST_CASE("Docks: a class ranked above or level with a larger one is not given its hulls")
{
	DockTests::Fixture fixture{false};
	Dock::Docks &docks = fixture.docks;
	// Each is a class by itself; the ranks are the game's, and these do not follow the sizes.
	docks.DefineBerthClass("long", {0, {140.0f, 50.0f, 30.0f}, {80.0f, 180.0f}});
	docks.DefineBerthClass("short", {1, {25.0f, 20.0f, 8.0f}, {35.0f, 40.0f}});
	docks.DefineBerthClass("level", {0, {25.0f, 20.0f, 8.0f}, {35.0f, 40.0f}});
	docks.DefineVehicle("barge", {"Barge", 130.0f, 45.0f, 25.0f, "long", {}});
	docks.DefineVehicle("pod", {"Pod", 18.0f, 12.0f, 5.0f, "level", {}});
	CHECK_FALSE(docks.Fits(*docks.FindVehicle("barge"), "short"));
	CHECK_FALSE(docks.Fits(*docks.FindVehicle("barge"), "level"));
	CHECK(docks.Fits(*docks.FindVehicle("barge"), "long"));
	// Level in rank and within its largest vehicle: a pod lands on a long berth, and on a short one.
	CHECK(docks.Fits(*docks.FindVehicle("pod"), "long"));
	CHECK(docks.Fits(*docks.FindVehicle("pod"), "short"));

	Dock::PlatformLayout dock;
	dock.displayName = "Dock";
	dock.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	dock.berths = {{"short", {0.0, 10.0, 0.0}}};
	docks.DefineLayout("dock", dock);
	const entt::entity platform =
	    docks.MakePlatform(fixture.world, DockTests::kHarbour, DockTests::kFerry, "dock");
	const entt::entity barge = fixture.Vehicle(platform, "barge", {0.0, 2000.0, 0.0});
	fixture.world.Commands().Push(Dock::RequestBerth{{}, barge, platform, false});
	fixture.Step();
	CHECK(fixture.NumberOf(barge) == 0);
	REQUIRE(fixture.Events<Dock::BerthRefused>().size() == 1);
	CHECK(fixture.Events<Dock::BerthRefused>()[0].reason.key == "reason.dock.no_berth_fits");
}


TEST_CASE("Docks: a ship asking for a feature is given only a pad that offers it, and queues for one")
{
	DockTests::Fixture fixture{false};
	Dock::Docks &docks = fixture.docks;
	for (const auto &[name, berthClass] : DockTests::SampleClasses())
		docks.DefineBerthClass(name, berthClass);
	docks.DefineVehicle("skiff", {"Skiff", 9.8f, 6.4f, 1.8f, "corvette", {}});
	// Berth 1 a plain corvette's, berth 2 a frigate's that offers a dry dock, berth 3 one for display
	// that says it does, which no vehicle is given.
	Dock::PlatformLayout yard;
	yard.displayName = "Yard";
	yard.decks = {{{0.0, 0.0, 0.0}, {400.0, 20.0, 400.0}}};
	yard.berths = {
	    {"corvette", {-100.0, 10.0, 0.0}},
	    {"frigate", {0.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}, false, {}, {"dry-dock"}},
	    {"corvette", {100.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, -1.0}, true, {}, {"dry-dock"}},
	};
	docks.DefineLayout("yard", yard);
	const entt::entity platform =
	    docks.MakePlatform(fixture.world, DockTests::kHarbour, DockTests::kFerry, "yard");
	const entt::registry &registry = fixture.world.Registry();
	CHECK_FALSE(docks.Offers(registry, fixture.BerthNumbered(platform, 1), "dry-dock"));
	CHECK(docks.Offers(registry, fixture.BerthNumbered(platform, 2), "dry-dock"));
	CHECK_FALSE(docks.Offers(registry, fixture.BerthNumbered(platform, 2), "paint"));
	CHECK_FALSE(docks.Offers(registry, platform, "dry-dock"));

	// The first is given the dry dock over the smaller free berth, which the second, asking for none,
	// takes; the third waits for the dry dock; the fourth asks for what no berth offers.
	const entt::entity first = fixture.Vehicle(platform, "skiff", {0.0, 2000.0, 0.0});
	const entt::entity second = fixture.Vehicle(platform, "skiff", {300.0, 2000.0, 0.0});
	const entt::entity third = fixture.Vehicle(platform, "skiff", {600.0, 2000.0, 0.0});
	const entt::entity fourth = fixture.Vehicle(platform, "skiff", {900.0, 2000.0, 0.0});
	fixture.world.Commands().Push(Dock::RequestBerth{{}, first, platform, false, "dry-dock"});
	fixture.world.Commands().Push(Dock::RequestBerth{{}, second, platform, false});
	fixture.world.Commands().Push(Dock::RequestBerth{{}, third, platform, false, "dry-dock"});
	fixture.world.Commands().Push(Dock::RequestBerth{{}, fourth, platform, false, "paint"});
	fixture.Step();
	CHECK(fixture.NumberOf(first) == 2);
	CHECK(fixture.NumberOf(second) == 1);
	CHECK(fixture.NumberOf(third) == 0);
	CHECK(registry.get<Dock::Docking>(third).stage == Dock::Stage::Queued);
	CHECK_FALSE(registry.all_of<Dock::Docking>(fourth));
	REQUIRE(fixture.Events<Dock::BerthRefused>().size() == 1);
	CHECK(fixture.Events<Dock::BerthRefused>()[0].vehicle == fourth);
	CHECK(fixture.Events<Dock::BerthRefused>()[0].reason.key == "reason.dock.no_berth_offers");

	// The plain berth coming free is not the third's; the dry dock is.
	fixture.world.Commands().Push(Dock::ReleaseBerth{{}, second});
	fixture.Step();
	CHECK(fixture.NumberOf(third) == 0);
	fixture.world.Commands().Push(Dock::ReleaseBerth{{}, first});
	fixture.Step();
	CHECK(fixture.NumberOf(third) == 2);

	CHECK(registry.get<Dock::Docking>(third).feature == "dry-dock");
}


TEST_CASE("Dock definitions: a layout's pad keeps its features through its file")
{
	Dock::RegisterTypes();
	Dock::PlatformLayout layout;
	layout.decks = {{{0.0, 0.0, 0.0}, {200.0, 20.0, 200.0}}};
	layout.berths = {
	    {"frigate",
	     {0.0, 10.0, 0.0},
	     {0.0, 1.0, 0.0},
	     {0.0, 0.0, -1.0},
	     false,
	     {},
	     {"dry-dock", "fuel"}},
	    {"frigate", {0.0, 10.0, 90.0}}};
	const auto read = Serialise::YamlArchive::Read<Dock::PlatformLayout>(
	    Serialise::YamlArchive::Write(layout, {Dock::kPlatformVersion}), Dock::PlatformMigrations());
	REQUIRE(read.berths.size() == 2);
	CHECK(read.berths[0].features == std::vector<std::string>{"dry-dock", "fuel"});
	CHECK(read.berths[1].features.empty());
	// A file that leaves the field out, as every one written before it does, reads as offering none.
	const auto bare = Serialise::YamlArchive::Read<Dock::PlatformLayout>(
	    "version: 1\npads:\n  - {class: frigate, position: [0, 10, 0]}\n", Dock::PlatformMigrations());
	REQUIRE(bare.berths.size() == 1);
	CHECK(bare.berths[0].features.empty());
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
	for (const entt::entity vehicle : {first, second, barge, waiting, skiff, small, last})
		yard.Ask(vehicle);
	yard.Step();
	// The skiff behind the waiting hauler takes the corvette berth, which the hauler does not fit; the
	// next skiff finds nothing, the display berth being no one's.
	CHECK(
	    yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{
	                        {first, 1}, {second, 5}, {barge, 3}, {skiff, 2}});
	REQUIRE(
	    yard.world.Registry().get<Dock::Platform>(yard.platform).queue ==
	    std::vector<entt::entity>{waiting, small, last});

	// Berths come free one at a time, and each goes to the first in the queue it fits.
	yard.events.clear();
	yard.world.Commands().Push(Dock::ReleaseBerth{{}, barge});
	yard.Step();
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{waiting, 3}});
	yard.events.clear();
	yard.world.Commands().Push(Dock::ReleaseBerth{{}, skiff});
	yard.world.Commands().Push(Dock::ReleaseBerth{{}, second});
	yard.Step();
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{small, 2}, {last, 5}});
	CHECK(yard.world.Registry().get<Dock::Platform>(yard.platform).queue.empty());
	CHECK(yard.Events<Dock::BerthReleased>().size() == 2);
}


TEST_CASE("Docks: a reservation not landed on in time lapses, and the pad goes to the next in line")
{
	Yard yard;
	// The frigate and destroyer berths held by vehicles parked on them, which hold them for good.
	for (const auto &[vehicle, number] :
	     std::vector<std::pair<std::string, std::uint32_t>>{{"hauler", 1}, {"hauler", 5}, {"barge", 3}})
		yard.docks.Park(yard.world, yard.Waiting(vehicle), yard.BerthNumbered(yard.platform, number));
	const entt::entity late = yard.Waiting("skiff");
	const entt::entity next = yard.Waiting("skiff");
	yard.Ask(late);
	yard.Ask(next);
	yard.Step();
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{late, 2}});

	// Two seconds, 120 steps from the one it was given in.
	yard.Run(2.0 - DockTests::kStep * 1.5);
	CHECK(yard.Events<Dock::BerthLapsed>().empty());
	yard.Run(DockTests::kStep * 2.0);
	const std::vector<Dock::BerthLapsed> lapsed = yard.Events<Dock::BerthLapsed>();
	REQUIRE(lapsed.size() == 1);
	CHECK(lapsed[0].vehicle == late);
	CHECK(lapsed[0].number == 2);
	CHECK_FALSE(yard.world.Registry().all_of<Dock::Docking>(late));
	CHECK(yard.Given().back() == std::pair{next, std::uint32_t{2}});
}


TEST_CASE("Docks: a display pad is never given, and a ship parked holds its pad")
{
	Yard yard;
	for (const auto &[vehicle, number] : std::vector<std::pair<std::string, std::uint32_t>>{
	         {"hauler", 1}, {"skiff", 2}, {"barge", 3}, {"hauler", 5}})
		yard.docks.Park(yard.world, yard.Waiting(vehicle), yard.BerthNumbered(yard.platform, number));
	CHECK_THROWS_AS(
	    yard.docks.Park(yard.world, yard.Waiting("skiff"), yard.BerthNumbered(yard.platform, 4)),
	    std::invalid_argument);
	const entt::entity skiff = yard.Waiting("skiff");
	yard.Ask(skiff);
	yard.Run(1.0);
	CHECK(yard.Given().empty());
	CHECK(yard.Events<Dock::BerthQueued>().size() == 1);
	CHECK(yard.BerthOf(yard.BerthNumbered(yard.platform, 4)).state == Dock::BerthState::Free);
	// The parked vehicles are clamped where they rest, out of flight.
	const entt::entity parked = yard.BerthOf(yard.BerthNumbered(yard.platform, 2)).vehicle;
	CHECK(Dock::IsDocked(yard.world.Registry(), parked, yard.platform));
	CHECK(yard.world.Registry().get<Physics::Body>(parked).kind == Physics::BodyKind::Kinematic);
}


TEST_CASE(
    "Docks: a request is refused, saying why, for a ship its sender does not control or cannot dock")
{
	Yard yard;
	entt::registry &registry = yard.world.Registry();
	const entt::entity vehicle = yard.Waiting("skiff");
	const Sim::Controller one{Sim::ControllerKind::Player, 1};
	const Sim::Controller two{Sim::ControllerKind::Player, 2};
	registry.emplace<Sim::ControlledBy>(vehicle, one);
	yard.Ask(vehicle, two);
	yard.world.Commands().Push(Dock::ReleaseBerth{{}, vehicle});
	yard.Ask(vehicle, one);
	yard.Ask(vehicle, one);
	yard.world.Commands().Push(Dock::RequestBerth{{}, vehicle, vehicle, false});
	yard.Ask(yard.Waiting("liner"));
	const entt::entity away = yard.Waiting("skiff");
	registry.replace<Sim::InFrame>(away, Sim::FrameId("Vesper"));
	registry.remove<Physics::LocalFrame>(away);
	yard.Ask(away);
	yard.world.Commands().Push(Dock::ReleaseBerth{two, vehicle});
	yard.Step();
	const auto platform = std::format("entity {:#x}", entt::to_integral(yard.platform));
	CHECK(
	    yard.Refusals() ==
	    std::vector<std::string>{
	        "reason.dock.not_controller()", "reason.dock.holds_nothing()",
	        std::format("reason.dock.already_holds_berth(2, {})", platform),
	        "reason.dock.not_a_platform()", std::format("reason.dock.no_berth_fits({})", platform),
	        std::format("reason.dock.other_frame({})", platform), "reason.dock.not_controller()"});
	CHECK(yard.Given() == std::vector<std::pair<entt::entity, std::uint32_t>>{{vehicle, 2}});
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

	// The other vehicle set down on the given one's berth, at rest, turned to it: not clamped.
	yard.PutOn(other, 2, 1.8);
	yard.Run(0.5);
	CHECK(yard.Events<Dock::Landed>().empty());
	CHECK(yard.world.Registry().get<Physics::Body>(other).kind == Physics::BodyKind::Dynamic);

	// Its own vehicle is.
	yard.PutOn(other, 1, 1.8);
	yard.PutOn(given, 2, 1.8);
	yard.Step();
	const std::vector<Dock::Landed> landed = yard.Events<Dock::Landed>();
	REQUIRE(landed.size() == 2);
	CHECK(landed[0].vehicle == given);
	CHECK(landed[0].number == 2);
	CHECK(Dock::IsDocked(yard.world.Registry(), given, yard.platform));
	CHECK(yard.BerthOf(yard.BerthNumbered(yard.platform, 2)).state == Dock::BerthState::Holding);
}


namespace
{
// A berth as version 1 had it, its class an enumerator of the closed four.
enum class BerthClassV1 : std::uint8_t
{
	S,
	M,
	L,
	XL
};

struct BerthV1
{
	entt::entity platform{entt::null};
	std::uint32_t number{0};
	BerthClassV1 berthClass{BerthClassV1::S};
	bool display{false};
	Dock::BerthState state{Dock::BerthState::Free};
	entt::entity vehicle{entt::null};
	std::uint64_t lapses{0};
};
} // namespace


TEST_CASE("Docks: a pad saved with its class an enumerator loads with the class's name")
{
	using Reflect::Attribute;
	Dock::RegisterTypes();
	Reflect::RegisterEnum<BerthClassV1>("DockTests.PadClassV1")
	    .Value<BerthClassV1::S>("S")
	    .Value<BerthClassV1::M>("M")
	    .Value<BerthClassV1::L>("L")
	    .Value<BerthClassV1::XL>("XL");
	Reflect::RegisterType<BerthV1>("DockTests.PadV1")
	    .Field<&BerthV1::platform>("station", Attribute::Saved)
	    .Field<&BerthV1::number>("number", Attribute::Saved)
	    .Field<&BerthV1::berthClass>("padClass", Attribute::Saved)
	    .Field<&BerthV1::display>("display", Attribute::Saved)
	    .Field<&BerthV1::state>("state", Attribute::Saved)
	    .Field<&BerthV1::vehicle>("ship", Attribute::Saved)
	    .Field<&BerthV1::lapses>("lapses", Attribute::Saved);

	Sim::SimWorld old{DockTests::kStep, 1};
	old.RegisterComponent<BerthV1>();
	const entt::entity platform = old.Registry().create();
	old.Registry().emplace<BerthV1>(
	    old.Registry().create(),
	    BerthV1{platform, 3, BerthClassV1::XL, true, Dock::BerthState::Holding, platform, 42});
	Serialise::BinaryDocument document = Serialise::BinaryDocument::Parse(old.Save(), "Jettison.World");
	// As the world before the space extraction's renames saved it (version 4), under the name it gave a
	// berth.
	document.version = 4;
	document.Rename("DockTests.PadV1", "Dock.Pad");

	Sim::SimWorld world{DockTests::kStep, 1};
	world.RegisterComponent<Dock::Berth>();
	world.Load(document.Emit());
	CHECK(
	    world.Registry().get<Dock::Berth>(entt::entity{1}) ==
	    Dock::Berth{platform, 3, "XL", true, Dock::BerthState::Holding, platform, 42});
}
