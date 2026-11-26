#include "Fixture.h"

#include <jettison/dock/Definitions.h>
#include <jettison/serialise/Archive.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <string>

using namespace Jettison;

namespace
{
Dock::PlatformLayout Slab()
{
	// A deck 200 m square with its top at y = 10, and one frigate berth on it.
	Dock::PlatformLayout layout;
	layout.decks = {{{0.0, 0.0, 0.0}, {200.0, 20.0, 200.0}}};
	layout.berths = {{"frigate", {0.0, 10.0, 0.0}}};
	return layout;
}


std::string Refusal(const Dock::PlatformLayout &layout)
{
	try
	{
		Dock::CheckLayout("slab", layout, DockTests::SampleClasses());
	}
	catch (const Serialise::FormatError &error)
	{
		return error.what();
	}
	return {};
}


std::string Refusal(const Dock::VehicleDefinition &vehicle)
{
	try
	{
		Dock::CheckVehicle("hull", vehicle, DockTests::SampleClasses());
	}
	catch (const Serialise::FormatError &error)
	{
		return error.what();
	}
	return {};
}
} // namespace


TEST_CASE("Dock definitions: the sample's hulls and stations load, and are at the scales decided")
{
	DockTests::Fixture fixture;
	const Dock::Docks &docks = fixture.docks;
	const Dock::VehicleDefinition *shuttle = docks.FindVehicle("shuttle");
	REQUIRE(shuttle);
	CHECK(docks.DefinedBerthClasses() == DockTests::SampleClasses());
	CHECK(shuttle->berthClass == "corvette");
	CHECK(fixture.physics.HasShape(Dock::Docks::VehicleShape("shuttle")));
	REQUIRE(docks.FindVehicle("heavy-transport"));
	CHECK(docks.FindVehicle("heavy-transport")->berthClass == "destroyer");

	// Twelve destroyer berths at Highport, a kilometre and a half across: within the 2 km cluster range
	// from its centre, and inside the gates at 5 and 6 km. Its display wing's six beyond them, two
	// cruiser bays and four corvette berths, each showing a vehicle that fits it, inside the sphere the
	// piers make.
	const Dock::PlatformLayout *highport = docks.FindLayout("ostra-highport");
	REQUIRE(highport);
	REQUIRE(highport->berths.size() == 18);
	for (std::size_t index = 0; index < 12; ++index)
	{
		CHECK(highport->berths[index].berthClass == "destroyer");
		CHECK(!highport->berths[index].display);
		CHECK(highport->berths[index].vehicle.empty());
	}
	for (std::size_t index = 12; index < 18; ++index)
	{
		const Dock::BerthLayout &berth = highport->berths[index];
		CHECK(berth.display);
		const Dock::VehicleDefinition *shown = docks.FindVehicle(berth.vehicle);
		REQUIRE(shown);
		CHECK(docks.Fits(*shown, berth.berthClass));
	}
	CHECK(std::ranges::count(highport->berths, "cruiser", &Dock::BerthLayout::berthClass) == 2);
	CHECK(docks.FindVehicle("liner")->berthClass == "cruiser");
	CHECK(docks.FindVehicle("cruiser")->berthClass == "cruiser");
	CHECK(Dock::Radius(*highport) == doctest::Approx(762.4).epsilon(0.001));

	// Its deck as the platform collides: twelve triangles a box, every one facing out of its box.
	const Physics::ShapeDefinition deck = Dock::DeckShape(*highport);
	CHECK(deck.indices.size() == 6 * 12 * 3);
	const std::vector<Dock::Box> boxes = [&]
	{
		std::vector<Dock::Box> all = highport->decks;
		all.insert(all.end(), highport->piers.begin(), highport->piers.end());
		return all;
	}();
	for (std::size_t triangle = 0; triangle < deck.indices.size() / 3; ++triangle)
	{
		const glm::dvec3 a{deck.vertices[deck.indices[triangle * 3]]};
		const glm::dvec3 b{deck.vertices[deck.indices[triangle * 3 + 1]]};
		const glm::dvec3 c{deck.vertices[deck.indices[triangle * 3 + 2]]};
		const glm::dvec3 centre = boxes[triangle / 12].centre;
		CHECK(glm::dot(glm::cross(b - a, c - a), (a + b + c) / 3.0 - centre) > 0.0);
	}

	// Four frigate berths and two corvette at Corva, 600 m across.
	const Dock::PlatformLayout *corva = docks.FindLayout("corva-refinery");
	REQUIRE(corva);
	CHECK(std::ranges::count(corva->berths, "frigate", &Dock::BerthLayout::berthClass) == 4);
	CHECK(std::ranges::count(corva->berths, "corvette", &Dock::BerthLayout::berthClass) == 2);
	CHECK(Dock::Radius(*corva) == doctest::Approx(302.8).epsilon(0.001));
}


TEST_CASE("Dock definitions: a hull larger than its pad's largest, or of no size, is refused")
{
	Dock::VehicleDefinition fighter{"Fighter", 18.0f, 14.0f, 5.0f, "corvette"};
	CHECK(Refusal(fighter).empty());
	Dock::VehicleDefinition longer = fighter;
	longer.length = 26.0f;
	CHECK(
	    Refusal(longer) ==
	    "Vehicle 'hull' is 26 x 14 x 5 m, larger than its berth's largest vehicle, 25 x 20 x 8 m");
	longer.berthClass = "frigate";
	CHECK(Refusal(longer).empty());
	Dock::VehicleDefinition flat = fighter;
	flat.height = 0.0f;
	CHECK(Refusal(flat) == "Vehicle 'hull' has a length, beam or height that is not a positive size");
}


TEST_CASE("Dock definitions: a pad off its deck, under it, overlapping another or overhung is refused")
{
	CHECK(Refusal(Slab()).empty());

	Dock::PlatformLayout off = Slab();
	off.berths[0].position.x = 90.0;
	CHECK(Refusal(off) == "Platform layout 'slab' berth 1 is not all on a deck or a pier");

	// Its middle inside the deck, and then on the deck's underside, facing down.
	Dock::PlatformLayout under = Slab();
	under.berths[0].position.y = 0.0;
	CHECK(Refusal(under) == "Platform layout 'slab' berth 1 is under a deck or a pier, not on one");
	under.berths[0].position.y = -10.0;
	under.berths[0].normal = {0.0, -1.0, 0.0};
	CHECK(Refusal(under).empty());

	// Two frigate berths end to end along the deck, 90 m long each, and then one moved onto the other.
	Dock::PlatformLayout two = Slab();
	two.berths = {{"frigate", {0.0, 10.0, -50.0}}, {"frigate", {0.0, 10.0, 50.0}}};
	CHECK(Refusal(two).empty());
	two.berths[1].position.z = 30.0;
	CHECK(Refusal(two) == "Platform layout 'slab' berth 2 overlaps berth 1");

	// A block standing on the deck beside the berth is fine; one over it is not.
	Dock::PlatformLayout overhung = Slab();
	overhung.decks.push_back({{60.0, 40.0, 0.0}, {40.0, 60.0, 40.0}});
	CHECK(Refusal(overhung).empty());
	overhung.decks.push_back({{0.0, 200.0, 0.0}, {10.0, 10.0, 10.0}});
	CHECK(Refusal(overhung) == "Platform layout 'slab' berth 1 has the platform over it");

	Dock::PlatformLayout skew = Slab();
	skew.berths[0].heading = {0.0, 0.6, -0.8};
	CHECK(
	    Refusal(skew) ==
	    "Platform layout 'slab' berth 1 has a heading that is not square to its normal");
	skew.berths[0].heading = {0.0, 0.0, -2.0};
	CHECK(
	    Refusal(skew) ==
	    "Platform layout 'slab' berth 1 has a normal or a heading that is not of unit length");

	Dock::PlatformLayout empty;
	CHECK(Refusal(empty) == "Platform layout 'slab' has no decks or piers");

	// A vehicle shown is the dressing of a display berth: on any other, a vehicle lands.
	Dock::PlatformLayout shown = Slab();
	shown.berths[0].vehicle = "transport";
	CHECK(
	    Refusal(shown) == "Platform layout 'slab' berth 1 shows a vehicle, which only a display berth "
	                      "does: a vehicle lands on any "
	                      "other");
	shown.berths[0].display = true;
	CHECK(Refusal(shown).empty());
	Dock::PlatformLayout hasty = Slab();
	hasty.reservationSeconds = 0.0f;
	CHECK(
	    Refusal(hasty) ==
	    "Platform layout 'slab' has a reservation that is not a positive number of seconds");
}


TEST_CASE("Dock definitions: a berth class too small for its hull, or a class not defined, is refused")
{
	const Dock::BerthClassDefinition berth{0, {6.0f, 4.0f, 3.0f}, {8.0f, 10.0f}};
	CHECK_NOTHROW(Dock::CheckBerthClass("berth", berth));
	Dock::BerthClassDefinition narrow = berth;
	narrow.berthSize.x = 3.0f;
	CHECK_THROWS_WITH_AS(
	    Dock::CheckBerthClass("berth", narrow),
	    "Berth class 'berth' has a largest vehicle of 6 x 4 m, length by beam, that its 3 x 10 m "
	    "berth, width "
	    "by length, does not hold",
	    Serialise::FormatError);
	Dock::BerthClassDefinition flat = berth;
	flat.largestVehicle.z = 0.0f;
	CHECK_THROWS_WITH_AS(
	    Dock::CheckBerthClass("berth", flat),
	    "Berth class 'berth' has a largest vehicle or a berth that is not of positive size",
	    Serialise::FormatError);

	Dock::VehicleDefinition pod{"Pod", 5.0f, 3.0f, 2.0f, "berth", {}};
	CHECK(Refusal(pod) == "Vehicle 'hull' names berth class 'berth', which is not defined");
	pod.berthClass.clear();
	CHECK(Refusal(pod) == "Vehicle 'hull' names no berth class");
	Dock::PlatformLayout slab = Slab();
	slab.berths[0].berthClass = "berth";
	CHECK(
	    Refusal(slab) ==
	    "Platform layout 'slab': berth 1 names berth class 'berth', which is not defined");
}


TEST_CASE("Dock definitions: a pad's orientation has its normal up and its heading ahead")
{
	const Dock::BerthLayout berth{"destroyer", {220.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}};
	const glm::dquat turn = Dock::BerthOrientation(berth);
	CHECK(glm::distance(turn * glm::dvec3{0.0, 1.0, 0.0}, berth.normal) < 1.0e-9);
	CHECK(glm::distance(turn * glm::dvec3{0.0, 0.0, -1.0}, berth.heading) < 1.0e-9);
}


TEST_CASE("Dock definitions: a hull's fit is asked of the pad classes alone, with no world")
{
	// A game's own skiff below the sample's classes, as a content validator holds them.
	Dock::BerthClasses classes = DockTests::SampleClasses();
	classes.emplace("skiff", Dock::BerthClassDefinition{0, {10.0f, 7.0f, 2.0f}, {12.0f, 14.0f}});
	classes.at("corvette").rank = 1;
	const Dock::VehicleDefinition skiff{"Skiff", 9.8f, 6.4f, 1.8f, "skiff", {}};
	const Dock::VehicleDefinition transport{"Transport", 56.0f, 28.0f, 14.0f, "frigate", {}};
	CHECK_NOTHROW(Dock::CheckVehicle("skiff", skiff, classes));
	CHECK(Dock::Fits(classes, skiff, "skiff"));
	CHECK(Dock::Fits(classes, skiff, "corvette"));
	CHECK(Dock::Fits(classes, transport, "destroyer"));
	CHECK_FALSE(Dock::Fits(classes, transport, "skiff"));
	CHECK_FALSE(Dock::Fits(classes, transport, "corvette"));
	CHECK_FALSE(Dock::Fits(classes, skiff, "berth"));
	Dock::VehicleDefinition stray = skiff;
	stray.berthClass = "berth";
	CHECK_FALSE(Dock::Fits(classes, stray, "cruiser"));
}
