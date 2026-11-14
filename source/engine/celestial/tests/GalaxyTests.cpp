#include <jettison/celestial/Galaxy.h>

#include <jettison/celestial/StarSystem.h>
#include <jettison/serialise/Archive.h>

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

using namespace Jettison::Celestial;
using Jettison::Serialise::FormatError;

namespace
{
// Three systems in a line and a fourth on a branch from the middle one.
const char *const kGalaxy = R"(version: 1
name: Test
systems:
  - name: Near
    system: systems/near.yaml
    scene: scene/near.yaml
  - name: Middle
    system: systems/middle.yaml
  - name: Far
    system: systems/far.yaml
  - name: Branch
    system: systems/branch.yaml
lanes:
  - a:
      system: Near
      jumpPoint: Near Gate
    b:
      system: Middle
      jumpPoint: West Gate
    transitSeconds: 30
  - a:
      system: Far
      jumpPoint: Far Gate
    b:
      system: Middle
      jumpPoint: East Gate
    transitSeconds: 45
    toll: 6000
  - a:
      system: Middle
      jumpPoint: South Gate
    b:
      system: Branch
      jumpPoint: Branch Gate
)";

const char *const kMiddle = R"(version: 1
name: Middle
bodies:
  - name: Middle
    kind: Star
    radius: 7.0e8
    gravitationalParameter: 1.32712440018e20
  - name: West Gate
    kind: JumpPoint
    parent: Middle
    orbit:
      semiMajorAxis: 4.0e11
  - name: East Gate
    kind: JumpPoint
    parent: Middle
    orbit:
      semiMajorAxis: 5.0e11
  - name: South Gate
    kind: JumpPoint
    parent: Middle
    orbit:
      semiMajorAxis: 6.0e11
  - name: Rock
    kind: Planet
    parent: Middle
    orbit:
      semiMajorAxis: 1.0e11
)";

std::string ReadFile(const std::string &path)
{
	std::ifstream file{path};
	std::stringstream text;
	text << file.rdbuf();
	return text.str();
}

std::string Replace(std::string text, const std::string &from, const std::string &to)
{
	const auto at = text.find(from);
	REQUIRE(at != std::string::npos);
	return text.replace(at, from.size(), to);
}
} // namespace

TEST_CASE("Galaxy: read, and each lane's ends resolved to their systems")
{
	const Galaxy galaxy = ReadGalaxy(kGalaxy);
	CHECK(galaxy.name == "Test");
	REQUIRE(galaxy.systems.size() == 4);
	CHECK(galaxy.Find("Far") == 2);
	CHECK(galaxy.Find("Nowhere") == -1);
	CHECK(galaxy.FindBySystemFile("systems/middle.yaml") == 1);
	CHECK(galaxy.FindBySystemFile("systems/nowhere.yaml") == -1);
	CHECK(galaxy.systems[0].scene == "scene/near.yaml");
	CHECK_FALSE(galaxy.systems[1].scene.has_value());
	REQUIRE(galaxy.lanes.size() == 3);
	CHECK(galaxy.lanes[1].a.systemIndex == 2);
	CHECK(galaxy.lanes[1].b.systemIndex == 1);
	CHECK(galaxy.lanes[2].transitSeconds == 0.0);
	// A lane names its toll, or is free.
	CHECK(galaxy.lanes[1].toll == 6000);
	CHECK(galaxy.lanes[0].toll == 0);
}

TEST_CASE("Galaxy: a system's exits are its lanes in the galaxy's order, seen from its end")
{
	const Galaxy galaxy = ReadGalaxy(kGalaxy);

	// The middle system is the b end of the first two lanes and the a end of the third: each is seen
	// from the middle, whichever end the file lists first.
	const std::vector<Exit> middle = Exits(galaxy, 1);
	REQUIRE(middle.size() == 3);
	CHECK(middle[0].lane == 0);
	CHECK(middle[0].jumpPoint == "West Gate");
	CHECK(middle[0].destination == 0);
	CHECK(middle[0].arrival == "Near Gate");
	CHECK(middle[0].transitSeconds == 30.0);
	CHECK(middle[1].jumpPoint == "East Gate");
	CHECK(middle[1].destination == 2);
	CHECK(middle[1].arrival == "Far Gate");
	CHECK(middle[1].toll == 6000);
	CHECK(middle[2].jumpPoint == "South Gate");
	CHECK(middle[2].destination == 3);

	const std::vector<Exit> near = Exits(galaxy, 0);
	REQUIRE(near.size() == 1);
	CHECK(near[0].jumpPoint == "Near Gate");
	CHECK(near[0].destination == 1);
	CHECK(near[0].arrival == "West Gate");

	// A lane is travelled both ways: out through one end is in through the other.
	const auto east = ExitThrough(galaxy, 1, "East Gate");
	REQUIRE(east);
	const auto back = ExitThrough(galaxy, east->destination, east->arrival);
	REQUIRE(back);
	CHECK(back->lane == east->lane);
	CHECK(back->destination == 1);
	CHECK(back->arrival == "East Gate");
	CHECK_FALSE(ExitThrough(galaxy, 1, "Rock"));
}

TEST_CASE("Galaxy: a written galaxy reads back as the same galaxy")
{
	const Galaxy galaxy = ReadGalaxy(kGalaxy);
	const std::string written = WriteGalaxy(galaxy);
	const Galaxy again = ReadGalaxy(written);
	CHECK(WriteGalaxy(again) == written);
	REQUIRE(again.lanes.size() == galaxy.lanes.size());
	CHECK(again.lanes[1].a.systemIndex == galaxy.lanes[1].a.systemIndex);
	CHECK_FALSE(again.systems[1].scene.has_value());
}

TEST_CASE("Galaxy: a graph that does not hold is refused, naming the system or the lane")
{
	const auto refuses = [](const std::string &text, const char *message)
	{ CHECK_THROWS_WITH_AS(ReadGalaxy(text), doctest::Contains(message), FormatError); };

	refuses(Replace(kGalaxy, "name: Far\n", "name: Near\n"), "system 'Near' is listed twice");
	refuses(Replace(kGalaxy, "name: Branch\n", "name: \"\"\n"), "system 3 has no name");
	refuses(
	    Replace(kGalaxy, "system: systems/far.yaml", "system: ../far.yaml"),
	    "system 'Far' has a system file '../far.yaml', which is not a virtual path");
	refuses(
	    Replace(kGalaxy, "scene: scene/near.yaml", "scene: /scene/near.yaml"),
	    "has a scene file '/scene/near.yaml'");
	refuses(
	    Replace(
	        kGalaxy, "system: Far\n      jumpPoint: Far Gate",
	        "system: Distant\n      jumpPoint: Far Gate"),
	    "lane 1 ('Distant' to 'Middle') ends in 'Distant', which the galaxy does not list");
	refuses(
	    Replace(
	        kGalaxy, "system: Branch\n      jumpPoint: Branch Gate",
	        "system: Middle\n      jumpPoint: Branch Gate"),
	    "lane 2 ('Middle' to 'Middle') joins a system to itself");
	refuses(
	    Replace(kGalaxy, "jumpPoint: East Gate", "jumpPoint: West Gate"),
	    "jump point 'West Gate' in 'Middle' is an end of lane 0 and of lane 1");
	refuses(Replace(kGalaxy, "jumpPoint: Far Gate", "jumpPoint: \"\""), "names no jump point in 'Far'");
	refuses(
	    Replace(kGalaxy, "transitSeconds: 45", "transitSeconds: -1"), "has a negative transit time");
	refuses(Replace(kGalaxy, "toll: 6000", "toll: -1"), "has a negative toll");
	refuses("version: 1\nname: Empty\nsystems: []\nlanes: []\n", "lists no systems");
}

TEST_CASE("Galaxy: a system's file is checked against the lanes that end in it")
{
	const Galaxy galaxy = ReadGalaxy(kGalaxy);
	const StarSystem middle = ReadStarSystem(kMiddle);
	CHECK_NOTHROW(CheckJumpPoints(galaxy, 1, middle));

	const auto refuses = [&galaxy](std::size_t system, const std::string &text, const char *message)
	{
		const StarSystem starSystem = ReadStarSystem(text);
		CHECK_THROWS_WITH_AS(
		    CheckJumpPoints(galaxy, system, starSystem), doctest::Contains(message), FormatError);
	};
	refuses(
	    2, kMiddle, "system 'Far' is at 'systems/far.yaml', and the system there is named 'Middle'");
	refuses(
	    1, Replace(kMiddle, "name: East Gate", "name: Eastern Gate"),
	    "leaves 'Middle' through 'East Gate', which the system does not have");
	refuses(
	    1,
	    Replace(
	        kMiddle, "name: South Gate\n    kind: JumpPoint", "name: South Gate\n    kind: Station"),
	    "leaves 'Middle' through 'South Gate', which is not a jump point");
}

TEST_CASE("Galaxy: the sample galaxy in the assets reads, and every lane ends at a jump point")
{
	const Galaxy galaxy = ReadGalaxy(ReadFile(JETTISON_ASSET_SOURCE_DIR "/systems/galaxy.yaml"));
	REQUIRE(galaxy.systems.size() == 3);
	CHECK(galaxy.systems[0].name == "Halcyon");
	CHECK(galaxy.systems[0].scene == "scene/orbit.yaml");
	for (std::size_t system = 0; system < galaxy.systems.size(); ++system)
	{
		CAPTURE(galaxy.systems[system].name);
		const StarSystem starSystem =
		    ReadStarSystem(ReadFile(JETTISON_ASSET_SOURCE_DIR "/" + galaxy.systems[system].system));
		CHECK_NOTHROW(CheckJumpPoints(galaxy, system, starSystem));
		CHECK_FALSE(Exits(galaxy, system).empty());
	}
	// Halcyon to Marrow is two jumps, through Vesper.
	REQUIRE(Exits(galaxy, 0).size() == 1);
	CHECK(galaxy.systems[Exits(galaxy, 0)[0].destination].name == "Vesper");
	CHECK(Exits(galaxy, 1).size() == 2);
	// The star gates' lane is dearer than the moon gates'.
	CHECK(galaxy.lanes[0].toll == 2000);
	CHECK(galaxy.lanes[1].toll == 6000);
}
