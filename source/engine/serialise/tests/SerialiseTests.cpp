#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>

#include <doctest/doctest.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace Jettison::Serialise;
using Jettison::Reflect::Attribute;

namespace
{
enum class Good
{
	Ore,
	Water,
	Fuel,
	Contraband, // Not registered, so it has no name to write.
};

struct Hold
{
	Good good{Good::Ore};
	std::uint32_t quantity{0};
	double mass{0.0};

	bool operator==(const Hold &) const = default;
};

struct Ship
{
	std::string name;
	bool docked{false};
	std::int8_t crew{0};
	std::int64_t credits{0};
	std::uint64_t serial{0};
	float throttle{0.0f};
	glm::vec3 heading{0.0f, 0.0f, 1.0f};
	glm::dvec3 position{0.0};
	glm::dquat attitude{1.0, 0.0, 0.0, 0.0};
	glm::quat turret{1.0f, 0.0f, 0.0f, 0.0f};
	glm::vec4 colour{1.0f};
	std::vector<Hold> holds;
	std::vector<float> fuelHistory;
	std::vector<std::string> tags;
	std::vector<std::vector<int>> grid;
	std::vector<Good> manifest;
	// A default that differs from Hold's own, which a read must keep when the data leaves it out.
	Hold spare{.good = Good::Fuel, .quantity = 7, .mass = 1.5};
	// Runtime state: not Saved, so never written, and refused if a file names it.
	int reservedBy{0};

	bool operator==(const Ship &) const = default;
};

struct Untyped
{
};

struct HoldsUntyped
{
	Untyped inner;
};

struct VersionField
{
	int version{0};
};

// The tables are global and doctest runs every case in one process, so registration happens
// once, from whichever case runs first.
void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Jettison::Reflect;
		RegisterEnum<Good>("SerialiseTests.Good")
		    .Value<Good::Ore>("Ore")
		    .Value<Good::Water>("Water")
		    .Value<Good::Fuel>("Fuel");
		RegisterType<Hold>("SerialiseTests.Hold")
		    .Field<&Hold::good>("good", Attribute::Saved)
		    .Field<&Hold::quantity>("quantity", Attribute::Saved)
		    .Field<&Hold::mass>("mass", Attribute::Saved);
		RegisterType<Ship>("SerialiseTests.Ship")
		    .Field<&Ship::name>("name", Attribute::Saved)
		    .Field<&Ship::docked>("docked", Attribute::Saved)
		    .Field<&Ship::crew>("crew", Attribute::Saved)
		    .Field<&Ship::credits>("credits", Attribute::Saved)
		    .Field<&Ship::serial>("serial", Attribute::Saved)
		    .Field<&Ship::throttle>("throttle", Attribute::Saved)
		    .Field<&Ship::heading>("heading", Attribute::Saved)
		    .Field<&Ship::position>("position", Attribute::Saved)
		    .Field<&Ship::attitude>("attitude", Attribute::Saved)
		    .Field<&Ship::turret>("turret", Attribute::Saved)
		    .Field<&Ship::colour>("colour", Attribute::Saved)
		    .Field<&Ship::holds>("holds", Attribute::Saved)
		    .Field<&Ship::fuelHistory>("fuelHistory", Attribute::Saved)
		    .Field<&Ship::tags>("tags", Attribute::Saved)
		    .Field<&Ship::grid>("grid", Attribute::Saved)
		    .Field<&Ship::manifest>("manifest", Attribute::Saved)
		    .Field<&Ship::spare>("spare", Attribute::Saved)
		    .Field<&Ship::reservedBy>("reservedBy");
		RegisterType<HoldsUntyped>("SerialiseTests.HoldsUntyped")
		    .Field<&HoldsUntyped::inner>("inner", Attribute::Saved);
		RegisterType<VersionField>("SerialiseTests.VersionField")
		    .Field<&VersionField::version>("version", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

// A value with something awkward in every field: extremes of each integer width, floats with
// no short decimal form, strings YAML would read as something else if written bare.
Ship AwkwardShip()
{
	Ship ship;
	ship.name = "Hull #7: \"Persistence\"";
	ship.docked = true;
	ship.crew = std::numeric_limits<std::int8_t>::min();
	ship.credits = std::numeric_limits<std::int64_t>::min();
	ship.serial = std::numeric_limits<std::uint64_t>::max();
	ship.throttle = 0.1f;
	ship.heading = {-0.0f, 1e-30f, std::numeric_limits<float>::denorm_min()};
	ship.position = {1.495978707e11, -0.1, 6.02214076e23};
	ship.attitude = {0.5, -0.5, 0.5, -0.5};
	ship.turret = {std::sqrt(0.5f), 0.0f, -std::sqrt(0.5f), 0.0f};
	ship.colour = {0.2f, 1.0f / 3.0f, 3.4028235e38f, 0.0f};
	ship.holds = {{Good::Water, 40, 0.25}, {Good::Fuel, 4'000'000'000u, 1e-300}};
	ship.fuelHistory = {1.0f, 0.75f, 0.333333343f};
	ship.tags = {"",         "~",      "null",   "true",     "123",         "1.5",
	             "a: b",     "- dash", "# hash", " padded ", "line\nbreak", "tab\there",
	             "'single'", "Ωmega",  "[flow]", "{map}",    "&anchor"};
	ship.grid = {{1, 2}, {}, {-3}};
	ship.manifest = {Good::Fuel, Good::Ore, Good::Fuel};
	ship.spare = {Good::Water, 3, 2.0};
	ship.reservedBy = 0;
	return ship;
}
} // namespace

TEST_CASE("Serialise: a version mismatch names both versions")
{
	const FormatVersionError error{"scene", 3, 1};
	CHECK(std::string{error.what()} == "scene: version 3 found, version 1 expected");
}

TEST_CASE("Serialise: YAML round-trips every field type, awkward values included")
{
	RegisterTypes();
	const Ship ship = AwkwardShip();
	const std::string text = YamlArchive::Write(ship, {});
	CAPTURE(text);
	const Ship read = YamlArchive::Read<Ship>(text, {});
	CHECK(read == ship);
	// The sign of zero survives too, which == does not see.
	CHECK(std::signbit(read.heading.x));
	// Writing what was read gives the same text: nothing drifts on a second pass.
	CHECK(YamlArchive::Write(read, {}) == text);
}

TEST_CASE("Serialise: YAML writes the version, then Saved fields in registration order")
{
	RegisterTypes();
	CHECK(
	    YamlArchive::Write(Hold{Good::Water, 40, 0.25}, {3}) ==
	    "version: 3\ngood: Water\nquantity: 40\nmass: 0.25\n");

	Ship ship;
	ship.heading = {0.0f, 0.5f, 1.0f};
	ship.holds = {{Good::Fuel, 2, 0.5}};
	ship.fuelHistory = {0.1f, 0.2f};
	ship.reservedBy = 99;
	const std::string text = YamlArchive::Write(ship, {});
	CAPTURE(text);
	// Vectors and lists of leaves go on one line, as the scene format writes them; a float is
	// its shortest decimal, not its nine-digit expansion.
	CHECK(text.find("heading: [0, 0.5, 1]\n") != std::string::npos);
	// x, y, z, w: GLM's storage order, not the w-first order its constructor takes.
	CHECK(text.find("attitude: [0, 0, 0, 1]\n") != std::string::npos);
	CHECK(text.find("fuelHistory: [0.1, 0.2]\n") != std::string::npos);
	CHECK(text.find("holds:\n  - good: Fuel\n    quantity: 2\n    mass: 0.5\n") != std::string::npos);
	CHECK(text.find("reservedBy") == std::string::npos);
	CHECK(text.find("name: \"\"\n") != std::string::npos);

	// A string that would read as a bool or a number to another YAML reader is quoted.
	ship.tags = {"true", "no", "123", "-1.5", ".inf", "Ore", "12 parsecs"};
	CHECK(
	    YamlArchive::Write(ship, {}).find(
	        "tags: [\"true\", \"no\", \"123\", \"-1.5\", \".inf\", Ore, 12 parsecs]\n") !=
	    std::string::npos);
	CHECK(text.find("version: 1\nname:") == 0);
}

TEST_CASE("Serialise: YAML non-finite floats use YAML's spellings and read back")
{
	RegisterTypes();
	Ship ship;
	ship.throttle = std::numeric_limits<float>::quiet_NaN();
	ship.heading = {
	    std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 0.0f};
	ship.position = {
	    std::numeric_limits<double>::quiet_NaN(), 0.0, std::numeric_limits<double>::infinity()};
	const std::string text = YamlArchive::Write(ship, {});
	CHECK(text.find("throttle: .nan\n") != std::string::npos);
	CHECK(text.find("heading: [.inf, -.inf, 0]\n") != std::string::npos);

	const Ship read = YamlArchive::Read<Ship>(text, {});
	CHECK(std::isnan(read.throttle));
	CHECK(read.heading.x == std::numeric_limits<float>::infinity());
	CHECK(read.heading.y == -std::numeric_limits<float>::infinity());
	CHECK(std::isnan(read.position.x));
	CHECK(read.position.z == std::numeric_limits<double>::infinity());
	CHECK(
	    YamlArchive::Read<Ship>("version: 1\nthrottle: -.INF\n", {}).throttle ==
	    -std::numeric_limits<float>::infinity());
}

TEST_CASE("Serialise: YAML fields the data leaves out keep their defaults, nested ones included")
{
	RegisterTypes();
	const Ship read = YamlArchive::Read<Ship>(
	    "version: 1\n"
	    "name: Drifter\n"
	    "spare: {quantity: 9}\n"
	    "holds:\n"
	    "  - quantity: 5\n",
	    {});
	CHECK(read.name == "Drifter");
	CHECK(read.heading == glm::vec3{0.0f, 0.0f, 1.0f});
	// The owner's default for spare, not Hold's.
	CHECK(read.spare == Hold{Good::Fuel, 9, 1.5});
	// An element starts from the element type's default.
	REQUIRE(read.holds.size() == 1);
	CHECK(read.holds[0] == Hold{Good::Ore, 5, 0.0});
}

TEST_CASE("Serialise: YAML that does not fit the type is refused, naming the field and the line")
{
	RegisterTypes();
	const auto refuse = [](const std::string &text, const std::string &expected)
	{
		CAPTURE(text);
		CHECK_THROWS_WITH_AS(
		    YamlArchive::Read<Ship>(text, {}), doctest::Contains(expected.c_str()), FormatError);
	};

	refuse("name: Drifter\n", "SerialiseTests.Ship: the document has no version");
	refuse("- 1\n- 2\n", "the document is not a mapping");
	refuse("version: one\n", "the version 'one' is not a whole number");
	refuse("version: 1\nname: [unclosed\n", "SerialiseTests.Ship:");
	refuse(
	    "version: 1\nspeed: 4\n",
	    "SerialiseTests.Ship.speed: 'SerialiseTests.Ship' has no saved field 'speed' (line 2)");
	// Not Saved, so not readable either: a file cannot set runtime state.
	refuse("version: 1\nreservedBy: 4\n", "has no saved field 'reservedBy'");
	refuse(
	    "version: 1\ncrew: 1\ncrew: 2\n",
	    "SerialiseTests.Ship.crew: the field is given twice (line 3)");
	refuse(
	    "version: 1\nholds:\n  - mass: heavy\n",
	    "SerialiseTests.Ship.holds[0].mass: expected a number, found 'heavy' (line 3)");
	refuse("version: 1\ncrew: 128\n", "expected a whole number that fits a signed 8-bit, found '128'");
	refuse("version: 1\nserial: -1\n", "fits an unsigned 64-bit, found '-1'");
	refuse("version: 1\ncredits: 1.5\n", "found '1.5'");
	refuse("version: 1\ndocked: yes\n", "expected true or false, found 'yes'");
	refuse("version: 1\nthrottle: inf\n", "expected a number, found 'inf'");
	refuse(
	    "version: 1\nheading: [1, 2]\n",
	    "SerialiseTests.Ship.heading: expected a sequence of 3 numbers, found 2");
	refuse(
	    "version: 1\nmanifest: [Fuel, Contraband]\n",
	    "SerialiseTests.Ship.manifest[1]: 'Contraband' is not a 'SerialiseTests.Good'");
	refuse("version: 1\nholds: {good: Ore}\n", "expected a sequence, found a mapping");
	refuse("version: 1\nspare: 4\n", "expected a mapping of 'SerialiseTests.Hold' fields, found '4'");
	refuse("version: 1\nname: [a]\n", "expected a string, found a sequence");
	refuse("version: 1\nname:\n", "expected a string, found nothing");
}

TEST_CASE("Serialise: YAML of another version throws FormatVersionError")
{
	RegisterTypes();
	CHECK_THROWS_WITH_AS(
	    YamlArchive::Read<Hold>("version: 2\ngood: Ore\n", {1}),
	    "SerialiseTests.Hold: version 2 found, version 1 expected", FormatVersionError);
}

TEST_CASE("Serialise: binary round-trips every field type, and writes the same bytes twice")
{
	RegisterTypes();
	const Ship ship = AwkwardShip();
	const std::vector<std::byte> bytes = BinaryArchive::Write(ship, {4});
	const Ship read = BinaryArchive::Read<Ship>(bytes, {4});
	CHECK(read == ship);
	CHECK(std::signbit(read.heading.x));
	CHECK(BinaryArchive::Write(read, {4}) == bytes);

	Ship nonFinite;
	nonFinite.throttle = std::numeric_limits<float>::quiet_NaN();
	nonFinite.position.y = -std::numeric_limits<double>::infinity();
	const Ship readNonFinite = BinaryArchive::Read<Ship>(BinaryArchive::Write(nonFinite, {}), {});
	CHECK(std::isnan(readNonFinite.throttle));
	CHECK(readNonFinite.position.y == -std::numeric_limits<double>::infinity());
}

TEST_CASE("Serialise: binary is little-endian, with its names in a table")
{
	RegisterTypes();
	const std::vector<std::byte> bytes = BinaryArchive::Write(Hold{Good::Water, 0x01020304u, 0.0}, {2});
	const auto at = [&bytes](std::size_t offset) { return std::to_integer<int>(bytes.at(offset)); };
	CHECK(at(0) == 'J');
	CHECK(at(3) == 'N');
	CHECK(at(4) == 1); // layout revision, low byte first
	CHECK(at(5) == 0);

	// The quantity's four bytes, least significant first, somewhere after the header.
	const std::array<std::byte, 4> quantity{std::byte{4}, std::byte{3}, std::byte{2}, std::byte{1}};
	CHECK(std::ranges::search(bytes, quantity).begin() != bytes.end());
	// Names are text, the enumerator's included.
	const std::string asText(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	CHECK(asText.find("quantity") != std::string::npos);
	CHECK(asText.find("Water") != std::string::npos);
	CHECK(asText.find("SerialiseTests.Hold") != std::string::npos);
}

TEST_CASE("Serialise: binary that does not fit is refused: any truncation, extra bytes, another type")
{
	RegisterTypes();
	const std::vector<std::byte> bytes = BinaryArchive::Write(AwkwardShip(), {});
	const std::span<const std::byte> all{bytes};

	// Every proper prefix fails as a format error, never as a crash or a value.
	for (std::size_t size = 0; size < bytes.size(); ++size)
	{
		CAPTURE(size);
		CHECK_THROWS_AS(BinaryArchive::Read<Ship>(all.first(size), {}), FormatError);
	}

	std::vector<std::byte> longer = bytes;
	longer.push_back(std::byte{0});
	CHECK_THROWS_WITH_AS(
	    BinaryArchive::Read<Ship>(longer, {}), doctest::Contains("1 bytes follow the value"),
	    FormatError);

	std::vector<std::byte> notOurs = bytes;
	notOurs[0] = std::byte{'X'};
	CHECK_THROWS_WITH_AS(
	    BinaryArchive::Read<Ship>(notOurs, {}), doctest::Contains("not a Jettison binary archive"),
	    FormatError);

	CHECK_THROWS_WITH_AS(
	    BinaryArchive::Read<Hold>(bytes, {}),
	    doctest::Contains("the archive holds a 'SerialiseTests.Ship'"), FormatError);
	CHECK_THROWS_WITH_AS(
	    BinaryArchive::Read<Ship>(bytes, {2}),
	    "SerialiseTests.Ship: version 1 found, version 2 expected", FormatVersionError);
}

TEST_CASE("Serialise: binary reports a field whose length does not match its type")
{
	RegisterTypes();
	std::vector<std::byte> bytes = BinaryArchive::Write(Hold{Good::Ore, 9, 0.0}, {});
	// The quantity's record: its name's index (2, after "good" and "Ore"), then its length, 4.
	const std::array<std::byte, 8> record{std::byte{2}, {}, {}, {}, std::byte{4}, {}, {}, {}};
	const auto found = std::ranges::search(bytes, record);
	REQUIRE(found.begin() != bytes.end());
	found.begin()[4] = std::byte{2};
	CHECK_THROWS_WITH_AS(
	    BinaryArchive::Read<Hold>(bytes, {}),
	    doctest::Contains(
	        "SerialiseTests.Hold.quantity: the field holds 2 bytes, and its type reads 4"),
	    FormatError);

	// The one byte that differs between a docked ship and an undocked one is docked's.
	Ship docked;
	docked.docked = true;
	std::vector<std::byte> badBool = BinaryArchive::Write(docked, {});
	const std::vector<std::byte> unset = BinaryArchive::Write(Ship{}, {});
	REQUIRE(unset.size() == badBool.size());
	const auto differs = std::ranges::mismatch(unset, badBool);
	REQUIRE(differs.in2 != badBool.end());
	*differs.in2 = std::byte{2};
	CHECK_THROWS_WITH_AS(
	    BinaryArchive::Read<Ship>(badBool, {}), doctest::Contains("Ship.docked: 2 is not a bool"),
	    FormatError);
}

TEST_CASE("Serialise: a type an archive cannot write is a defect, reported as such")
{
	RegisterTypes();
	CHECK_THROWS_WITH_AS(
	    YamlArchive::Write(Untyped{}, {}),
	    doctest::Contains("an archive's root is a class registered with Reflect"),
	    UnsupportedTypeError);
	CHECK_THROWS_AS(BinaryArchive::Read<Untyped>({}, {}), UnsupportedTypeError);
	CHECK_THROWS_WITH_AS(
	    YamlArchive::Write(HoldsUntyped{}, {}),
	    doctest::Contains("SerialiseTests.HoldsUntyped.inner: type"), UnsupportedTypeError);
	CHECK_THROWS_AS(BinaryArchive::Write(HoldsUntyped{}, {}), UnsupportedTypeError);
	CHECK_THROWS_WITH_AS(
	    YamlArchive::Write(VersionField{}, {}),
	    doctest::Contains("cannot save a field named 'version'"), UnsupportedTypeError);

	Ship ship;
	ship.manifest = {Good::Contraband};
	CHECK_THROWS_WITH_AS(
	    YamlArchive::Write(ship, {}),
	    doctest::Contains("SerialiseTests.Ship.manifest[0]: the value is not an enumerator"),
	    UnsupportedTypeError);
	CHECK_THROWS_AS(BinaryArchive::Write(ship, {}), UnsupportedTypeError);
}

TEST_CASE("Serialise: binary streams carry a sequence of values under a format's name")
{
	RegisterTypes();
	BinaryWriter writer{"SerialiseTests.Stream"};
	writer.WriteUnsigned(0x0102030405060708u, 8);
	writer.WriteName("component");
	writer.Write(2.5);
	writer.Write(Hold{Good::Water, 12, 0.5});
	writer.Write(std::vector<Good>{Good::Fuel, Good::Ore});
	writer.Write(entt::entity{0x00100002u});
	writer.WriteName("component");
	const std::vector<std::byte> bytes = writer.Finish({7});

	BinaryReader reader{bytes, "SerialiseTests.Stream", {7}};
	CHECK(reader.ReadUnsigned(8) == 0x0102030405060708u);
	CHECK(reader.ReadName() == "component");
	double number = 0.0;
	reader.Read(number);
	CHECK(number == 2.5);
	// Read into an existing object, through a reference, as a world's loader reads a component.
	Hold hold{Good::Ore, 1, 9.0};
	reader.Read(hold);
	CHECK(hold == Hold{Good::Water, 12, 0.5});
	std::vector<Good> goods{Good::Water};
	reader.Read(goods);
	CHECK(goods == std::vector<Good>{Good::Fuel, Good::Ore});
	entt::entity entity{entt::null};
	reader.Read(entity);
	CHECK(entity == entt::entity{0x00100002u});
	CHECK(reader.ReadName() == "component");
	reader.End();

	CHECK_THROWS_WITH_AS(
	    (BinaryReader{bytes, "SerialiseTests.Other", {7}}),
	    doctest::Contains("the archive holds a 'SerialiseTests.Stream'"), FormatError);
	CHECK_THROWS_AS((BinaryReader{bytes, "SerialiseTests.Stream", {8}}), FormatVersionError);
	BinaryReader early{bytes, "SerialiseTests.Stream", {7}};
	CHECK_THROWS_WITH_AS(early.End(), doctest::Contains("bytes follow the value"), FormatError);
	CHECK_THROWS_AS(writer.WriteUnsigned(1, 3), std::invalid_argument);
}
