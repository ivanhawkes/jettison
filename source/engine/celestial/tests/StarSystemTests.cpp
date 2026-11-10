#include <jettison/celestial/StarSystem.h>

#include <jettison/serialise/Archive.h>

#include <doctest/doctest.h>

#include <bit>
#include <fstream>
#include <sstream>
#include <string>

using namespace Jettison::Celestial;
using Jettison::Serialise::FormatError;

namespace
{
const char *const kSystem = R"(version: 1
name: Test
bodies:
  - name: Sun
    kind: Star
    radius: 7.0e8
    gravitationalParameter: 1.32712440018e20
  - name: World
    kind: Planet
    parent: Sun
    radius: 6.371e6
    gravitationalParameter: 3.986004418e14
    orbit:
      semiMajorAxis: 1.496e11
      eccentricity: 0.0167
    rotation:
      period: 86164
      axialTilt: 0.4
  - name: Dock
    kind: Station
    parent: World
    radius: 60
    orbit:
      semiMajorAxis: 6.791e6
      inclination: 0.9
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

TEST_CASE("Star system: read, the hierarchy resolved and the periods derived")
{
	const StarSystem system = ReadStarSystem(kSystem);
	REQUIRE(system.bodies.size() == 3);
	CHECK(system.name == "Test");
	CHECK(system.bodies[0].parentIndex == -1);
	CHECK(system.bodies[1].parentIndex == 0);
	CHECK(system.bodies[2].parentIndex == 1);
	CHECK(system.bodies[2].kind == BodyKind::Station);
	CHECK(system.Find("Dock") == 2);
	CHECK(system.Find("Nowhere") == -1);
	// A sidereal year, and a low orbit's ninety-two minutes.
	CHECK(system.bodies[1].orbit.period / 86400.0 == doctest::Approx(365.25).epsilon(1e-3));
	CHECK(system.bodies[2].orbit.period == doctest::Approx(5569.6).epsilon(1e-4));
}

TEST_CASE("Star system: a written system reads back as the same system")
{
	const StarSystem system = ReadStarSystem(kSystem);
	const std::string written = WriteStarSystem(system);
	const StarSystem again = ReadStarSystem(written);
	CHECK(WriteStarSystem(again) == written);
	REQUIRE(again.bodies.size() == system.bodies.size());
	for (std::size_t index = 0; index < system.bodies.size(); ++index)
	{
		CHECK(again.bodies[index].orbit.period == system.bodies[index].orbit.period);
		CHECK(again.bodies[index].parentIndex == system.bodies[index].parentIndex);
	}
	// The runtime index is not part of the file.
	CHECK(written.find("parentIndex") == std::string::npos);
}

TEST_CASE("Star system: a hierarchy that does not hold is refused, naming the body")
{
	const auto refused = [](const std::string &text, const std::string &why)
	{
		CAPTURE(why);
		CHECK_THROWS_WITH_AS(ReadStarSystem(text), doctest::Contains(why.c_str()), FormatError);
	};
	refused(Replace(kSystem, "parent: World", "parent: Moon"), "'Dock' orbits 'Moon'");
	refused(Replace(kSystem, "name: Dock", "name: World"), "'World' is listed twice");
	refused(Replace(kSystem, "    parent: Sun\n", ""), "'World' names no parent");
	refused(Replace(kSystem, "eccentricity: 0.0167", "eccentricity: 1.2"), "outside [0, 1)");
	refused(Replace(kSystem, "radius: 60", "radius: -60"), "negative radius");
	refused(
	    Replace(kSystem, "    gravitationalParameter: 3.986004418e14\n", ""),
	    "'Dock' gives no period, and its parent 'World' no gravitational parameter");
	refused("version: 1\nname: Empty\nbodies: []\n", "has no bodies");

	// A child listed before its parent.
	StarSystem system = ReadStarSystem(kSystem);
	std::swap(system.bodies[1], system.bodies[2]);
	CHECK_THROWS_WITH_AS(
	    Resolve(system), doctest::Contains("'Dock' orbits 'World', which is not listed before it"),
	    FormatError);
}

TEST_CASE("Star system: positions add up the hierarchy, the same bits by either path")
{
	const StarSystem system = ReadStarSystem(kSystem);
	for (const double t : {0.0, 1234.5, 3.0e7})
	{
		const auto positions = EvaluateSystem(system, t);
		REQUIRE(positions.size() == 3);
		CHECK(positions[0].metres == glm::dvec3{0.0});
		CHECK(glm::length(positions[1].metres) == doctest::Approx(1.496e11).epsilon(0.0167 * 1.01));
		CHECK(glm::length(positions[2].metres - positions[1].metres) == doctest::Approx(6.791e6));
		for (std::size_t body = 0; body < positions.size(); ++body)
		{
			const glm::dvec3 alone = EvaluateBody(system, body, t).metres;
			for (int axis = 0; axis < 3; ++axis)
				CHECK(
				    std::bit_cast<std::uint64_t>(alone[axis]) ==
				    std::bit_cast<std::uint64_t>(positions[body].metres[axis]));
		}
	}
}

TEST_CASE("Star system: presentation time is interpolated between the steps")
{
	CHECK(PresentationTime(10.0, 11.0, 0.0) == 10.0);
	CHECK(PresentationTime(10.0, 11.0, 0.25) == 10.25);
}

TEST_CASE("Star system: the sample system in the assets reads")
{
	const StarSystem system =
	    ReadStarSystem(ReadFile(JETTISON_ASSET_SOURCE_DIR "/systems/halcyon.yaml"));
	CHECK(system.name == "Halcyon");
	const int station = system.Find("Ostra Highport");
	REQUIRE(station > 0);
	CHECK(system.bodies[static_cast<std::size_t>(station)].kind == BodyKind::Station);
	CHECK(system.bodies[static_cast<std::size_t>(station)].parentIndex == system.Find("Ostra"));
}
