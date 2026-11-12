#include "Fixture.h"

#include <jettison/dock/Definitions.h>
#include <jettison/serialise/Archive.h>

#include <doctest/doctest.h>

#include <glm/geometric.hpp>

#include <string>

using namespace Jettison;
using Dock::PadClass;

namespace
{
Dock::StationLayout Slab()
{
	// A deck 200 m square with its top at y = 10, and one M pad on it.
	Dock::StationLayout layout;
	layout.decks = {{{0.0, 0.0, 0.0}, {200.0, 20.0, 200.0}}};
	layout.pads = {{PadClass::M, {0.0, 10.0, 0.0}}};
	return layout;
}

std::string Refusal(const Dock::StationLayout &layout)
{
	try
	{
		Dock::CheckLayout("slab", layout);
	}
	catch (const Serialise::FormatError &error)
	{
		return error.what();
	}
	return {};
}

std::string Refusal(const Dock::HullDefinition &hull)
{
	try
	{
		Dock::CheckHull("hull", hull);
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
	const Dock::HullDefinition *shuttle = docks.FindHull("shuttle");
	REQUIRE(shuttle);
	CHECK(shuttle->pad == PadClass::S);
	CHECK(fixture.physics.HasShape(Dock::Docks::HullShape("shuttle")));
	REQUIRE(docks.FindHull("heavy-transport"));
	CHECK(docks.FindHull("heavy-transport")->pad == PadClass::L);

	// Twelve L pads at Highport, a kilometre and a half across: within the 2 km cluster range from
	// its centre, and inside the gates at 5 and 6 km. Its display wing's six beyond them, two XL bays
	// and four S pads, each showing a hull that fits it, inside the sphere the piers make.
	const Dock::StationLayout *highport = docks.FindLayout("ostra-highport");
	REQUIRE(highport);
	REQUIRE(highport->pads.size() == 18);
	for (std::size_t index = 0; index < 12; ++index)
	{
		CHECK(highport->pads[index].padClass == PadClass::L);
		CHECK(!highport->pads[index].display);
		CHECK(highport->pads[index].ship.empty());
	}
	for (std::size_t index = 12; index < 18; ++index)
	{
		const Dock::PadLayout &pad = highport->pads[index];
		CHECK(pad.display);
		const Dock::HullDefinition *shown = docks.FindHull(pad.ship);
		REQUIRE(shown);
		CHECK(shown->pad <= pad.padClass);
	}
	CHECK(std::ranges::count(highport->pads, PadClass::XL, &Dock::PadLayout::padClass) == 2);
	CHECK(docks.FindHull("liner")->pad == PadClass::XL);
	CHECK(docks.FindHull("frigate")->pad == PadClass::XL);
	CHECK(Dock::Radius(*highport) == doctest::Approx(762.4).epsilon(0.001));
	// Its deck as the station collides: twelve triangles a box, every one facing out of its box.
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

	// Four M pads and two S at Corva, 600 m across.
	const Dock::StationLayout *corva = docks.FindLayout("corva-refinery");
	REQUIRE(corva);
	CHECK(std::ranges::count(corva->pads, PadClass::M, &Dock::PadLayout::padClass) == 4);
	CHECK(std::ranges::count(corva->pads, PadClass::S, &Dock::PadLayout::padClass) == 2);
	CHECK(Dock::Radius(*corva) == doctest::Approx(302.8).epsilon(0.001));
}

TEST_CASE(
    "Dock definitions: a hull larger than its pad's largest, or with its hatch outside it, is refused")
{
	Dock::HullDefinition fighter{"Fighter", 18.0f, 14.0f, 5.0f, PadClass::S, {0.0f, -2.5f, 4.0f}};
	CHECK(Refusal(fighter).empty());
	Dock::HullDefinition longer = fighter;
	longer.length = 26.0f;
	CHECK(
	    Refusal(longer) ==
	    "Hull 'hull' is 26 x 14 x 5 m, larger than its pad's largest hull, 25 x 20 x 8 m");
	longer.pad = PadClass::M;
	CHECK(Refusal(longer).empty());
	Dock::HullDefinition flat = fighter;
	flat.height = 0.0f;
	CHECK(Refusal(flat) == "Hull 'hull' has a length, beam or height that is not a positive size");
	Dock::HullDefinition open = fighter;
	open.hatch = {0.0f, -3.0f, 4.0f};
	CHECK(Refusal(open) == "Hull 'hull' has its cargo hatch outside its hull");
}

TEST_CASE("Dock definitions: a pad off its deck, under it, overlapping another or overhung is refused")
{
	CHECK(Refusal(Slab()).empty());

	Dock::StationLayout off = Slab();
	off.pads[0].position.x = 90.0;
	CHECK(Refusal(off) == "Station layout 'slab' pad 1 is not all on a deck or a pier");
	// Its middle inside the deck, and then on the deck's underside, facing down.
	Dock::StationLayout under = Slab();
	under.pads[0].position.y = 0.0;
	CHECK(Refusal(under) == "Station layout 'slab' pad 1 is under a deck or a pier, not on one");
	under.pads[0].position.y = -10.0;
	under.pads[0].normal = {0.0, -1.0, 0.0};
	CHECK(Refusal(under).empty());

	// Two M pads end to end along the deck, 90 m long each, and then one moved onto the other.
	Dock::StationLayout two = Slab();
	two.pads = {{PadClass::M, {0.0, 10.0, -50.0}}, {PadClass::M, {0.0, 10.0, 50.0}}};
	CHECK(Refusal(two).empty());
	two.pads[1].position.z = 30.0;
	CHECK(Refusal(two) == "Station layout 'slab' pad 2 overlaps pad 1");

	// A block standing on the deck beside the pad is fine; one over it is not.
	Dock::StationLayout overhung = Slab();
	overhung.decks.push_back({{60.0, 40.0, 0.0}, {40.0, 60.0, 40.0}});
	CHECK(Refusal(overhung).empty());
	overhung.decks.push_back({{0.0, 200.0, 0.0}, {10.0, 10.0, 10.0}});
	CHECK(Refusal(overhung) == "Station layout 'slab' pad 1 has the station over it");

	Dock::StationLayout skew = Slab();
	skew.pads[0].heading = {0.0, 0.6, -0.8};
	CHECK(
	    Refusal(skew) == "Station layout 'slab' pad 1 has a heading that is not square to its normal");
	skew.pads[0].heading = {0.0, 0.0, -2.0};
	CHECK(
	    Refusal(skew) ==
	    "Station layout 'slab' pad 1 has a normal or a heading that is not of unit length");

	Dock::StationLayout empty;
	CHECK(Refusal(empty) == "Station layout 'slab' has no decks or piers");

	// A ship shown is the dressing of a display pad: on any other, a ship lands.
	Dock::StationLayout shown = Slab();
	shown.pads[0].ship = "transport";
	CHECK(
	    Refusal(shown) ==
	    "Station layout 'slab' pad 1 shows a ship, which only a display pad does: a ship lands on any "
	    "other");
	shown.pads[0].display = true;
	CHECK(Refusal(shown).empty());
	Dock::StationLayout hasty = Slab();
	hasty.reservationSeconds = 0.0f;
	CHECK(
	    Refusal(hasty) ==
	    "Station layout 'slab' has a reservation that is not a positive number of seconds");
}

TEST_CASE("Dock definitions: a pad's orientation has its normal up and its heading ahead")
{
	const Dock::PadLayout pad{PadClass::L, {220.0, 10.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}};
	const glm::dquat turn = Dock::PadOrientation(pad);
	CHECK(glm::distance(turn * glm::dvec3{0.0, 1.0, 0.0}, pad.normal) < 1.0e-9);
	CHECK(glm::distance(turn * glm::dvec3{0.0, 0.0, -1.0}, pad.heading) < 1.0e-9);
}
