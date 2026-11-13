#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Binary.h>
#include <jettison/serialise/Migration.h>

#include <doctest/doctest.h>

#include <yaml-cpp/yaml.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison::Serialise;
using Jettison::Reflect::Attribute;

namespace
{
// A format with a history. Version 1 called the cargo's weight its mass; version 2 renamed it
// tonnes; version 3 added where it came from, which version 2's data says only through its label.
struct Cargo
{
	std::string label;
	std::uint32_t tonnes{0};
	std::string origin{"unknown"};
};
// What the old versions wrote, kept as types for the step that needs to read them, under names of
// their own: a type is a format's root only when it is read as one.
struct CargoV1
{
	std::string label;
	std::uint32_t mass{0};
};
struct CargoV2
{
	std::string label;
	std::uint32_t tonnes{0};
};

constexpr std::string_view kCargo = "MigrationTests.Cargo";

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Jettison::Reflect;
		RegisterType<Cargo>("MigrationTests.Cargo")
		    .Field<&Cargo::label>("label", Attribute::Saved)
		    .Field<&Cargo::tonnes>("tonnes", Attribute::Saved)
		    .Field<&Cargo::origin>("origin", Attribute::Saved);
		RegisterType<CargoV1>("MigrationTests.CargoV1")
		    .Field<&CargoV1::label>("label", Attribute::Saved)
		    .Field<&CargoV1::mass>("mass", Attribute::Saved);
		RegisterType<CargoV2>("MigrationTests.CargoV2")
		    .Field<&CargoV2::label>("label", Attribute::Saved)
		    .Field<&CargoV2::tonnes>("tonnes", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

// Data as an old version wrote it: an old type's fields under the format's name.
template <typename T> std::vector<std::byte> Written(const T &value, std::uint32_t version)
{
	BinaryWriter writer{kCargo};
	writer.Write(value);
	return writer.Finish({version});
}

std::string OriginOf(const std::string &label)
{
	return label.starts_with("Halcyon") ? "Halcyon" : "unknown";
}

// The chain as the format's owner keeps it: a rename is an edit to the table alone, and the step
// that adds a field reads version 2 with the type that wrote it.
BinaryMigrations CargoMigrations()
{
	BinaryMigrations migrations{std::string{kCargo}, {3}};
	migrations.Add(1, [](BinaryDocument &document) { document.Rename("mass", "tonnes"); });
	migrations.Add(
	    2,
	    [](BinaryDocument &document)
	    {
		    const std::vector<std::byte> bytes = document.Emit();
		    BinaryReader reader{bytes, document.format, {2}};
		    CargoV2 old;
		    reader.Read(old);
		    reader.End();
		    BinaryWriter writer{document.format};
		    writer.Write(Cargo{old.label, old.tonnes, OriginOf(old.label)});
		    document = BinaryDocument::Parse(writer.Finish({3}), document.format);
	    });
	return migrations;
}

YamlMigrations CargoYamlMigrations()
{
	YamlMigrations migrations{std::string{kCargo}, {3}};
	migrations.Add(
	    1,
	    [](YAML::Node &root)
	    {
		    root["tonnes"] = root["mass"];
		    root.remove("mass");
	    });
	migrations.Add(
	    2, [](YAML::Node &root) { root["origin"] = OriginOf(root["label"].as<std::string>()); });
	return migrations;
}
} // namespace

TEST_CASE("Migration: binary data of every earlier version is read as the current one")
{
	RegisterTypes();
	const BinaryMigrations migrations = CargoMigrations();

	const auto first = BinaryArchive::Read<Cargo>(Written(CargoV1{"Halcyon ore", 40}, 1), migrations);
	CHECK(first.label == "Halcyon ore");
	CHECK(first.tonnes == 40);
	CHECK(first.origin == "Halcyon");

	const auto second = BinaryArchive::Read<Cargo>(Written(CargoV2{"Vesper ice", 12}, 2), migrations);
	CHECK(second.tonnes == 12);
	CHECK(second.origin == "unknown");

	// The current version is read as it is, with no step run.
	const auto current = BinaryArchive::Read<Cargo>(
	    BinaryArchive::Write(Cargo{"Marrow fuel", 3, "Marrow"}, {3}), migrations);
	CHECK(current.origin == "Marrow");

	// Migrated, it is what the current version would have written.
	CHECK(
	    Migrate(Written(CargoV1{"Halcyon ore", 40}, 1), migrations) ==
	    BinaryArchive::Write(Cargo{"Halcyon ore", 40, "Halcyon"}, {3}));
	// And the streams read it the same way.
	const std::vector<std::byte> old = Written(CargoV1{"Halcyon ore", 40}, 1);
	BinaryReader reader{old, migrations};
	Cargo read;
	reader.Read(read);
	reader.End();
	CHECK(read.tonnes == 40);
}

TEST_CASE("Migration: a version the chain does not reach is refused before any step runs")
{
	RegisterTypes();
	int ran = 0;
	BinaryMigrations gap{std::string{kCargo}, {3}};
	gap.Add(2, [&ran](BinaryDocument &) { ++ran; });
	CHECK(gap.Reaches(2));
	CHECK(gap.Reaches(3));
	CHECK_FALSE(gap.Reaches(1));
	CHECK_FALSE(gap.Reaches(4));
	CHECK_FALSE(gap.Reaches(0));

	try
	{
		BinaryArchive::Read<Cargo>(Written(CargoV1{"Halcyon ore", 40}, 1), gap);
		FAIL("version 1 has no step to version 2");
	}
	catch (const FormatVersionError &error)
	{
		CHECK(error.Found() == 1);
		CHECK(error.Expected() == 3);
		CHECK(
		    std::string{error.what()} ==
		    "MigrationTests.Cargo: version 1 found, version 3 expected: there is no migration from "
		    "version 1");
	}
	try
	{
		BinaryArchive::Read<Cargo>(BinaryArchive::Write(Cargo{}, {4}), gap);
		FAIL("version 4 is newer than the chain");
	}
	catch (const FormatVersionError &error)
	{
		CHECK(std::string{error.what()}.ends_with("it was written by a later build"));
	}
	CHECK(ran == 0);

	// Without a chain, any other version is refused as before.
	CHECK_THROWS_AS(
	    BinaryArchive::Read<Cargo>(Written(CargoV2{"Vesper ice", 12}, 2), FormatVersion{3}),
	    FormatVersionError);
	// Data that is not the format's is a FormatError, whatever its version.
	CHECK_THROWS_AS(Migrate(BinaryArchive::Write(CargoV1{}, {1}), CargoMigrations()), FormatError);
	// And a chain is the format's alone.
	CHECK_THROWS_AS(
	    BinaryArchive::Read<CargoV1>(BinaryArchive::Write(CargoV1{}, {1}), CargoMigrations()),
	    std::logic_error);
}

TEST_CASE("Migration: a chain's steps lead to its current version, once each")
{
	BinaryMigrations migrations{"Test", {3}};
	const auto step = [](BinaryDocument &) {};
	CHECK_THROWS_AS(migrations.Add(0, step), std::logic_error);
	CHECK_THROWS_AS(migrations.Add(3, step), std::logic_error);
	migrations.Add(1, step);
	CHECK_THROWS_AS(migrations.Add(1, step), std::logic_error);
	CHECK_THROWS_AS(migrations.Add(2, {}), std::logic_error);
}

TEST_CASE("Migration: a binary document emits the bytes it parsed, and renames through its table")
{
	RegisterTypes();
	const std::vector<std::byte> bytes = Written(CargoV1{"Halcyon ore", 40}, 1);
	BinaryDocument document = BinaryDocument::Parse(bytes, kCargo);
	CHECK(document.version == 1);
	CHECK(document.Emit() == bytes);
	CHECK(PeekVersion(bytes, kCargo) == 1);

	CHECK_THROWS_AS(document.Rename("weight", "tonnes"), std::logic_error);
	CHECK_THROWS_AS(document.Rename("mass", "label"), std::logic_error);
	document.Rename("mass", "tonnes");
	document.version = 2;
	const auto renamed = BinaryArchive::Read<CargoV2>(
	    [&]
	    {
		    // Read as the version 2 type, whose root name is its own.
		    BinaryDocument asV2 = document;
		    asV2.Rename(std::string{kCargo}, "MigrationTests.CargoV2");
		    return asV2.Emit();
	    }(),
	    FormatVersion{2});
	CHECK(renamed.tonnes == 40);

	CHECK_THROWS_AS(BinaryDocument::Parse(bytes, "Other"), FormatError);
	for (std::size_t size = 0; size < 20; ++size)
		CHECK_THROWS_AS(BinaryDocument::Parse(std::span{bytes}.first(size), kCargo), FormatError);
}

TEST_CASE("Migration: YAML of every earlier version is read as the current one, its lines kept")
{
	RegisterTypes();
	const YamlMigrations migrations = CargoYamlMigrations();
	const auto first =
	    YamlArchive::Read<Cargo>("version: 1\nlabel: Halcyon ore\nmass: 40\n", migrations);
	CHECK(first.tonnes == 40);
	CHECK(first.origin == "Halcyon");
	const auto second =
	    YamlArchive::Read<Cargo>("version: 2\nlabel: Vesper ice\ntonnes: 12\n", migrations);
	CHECK(second.origin == "unknown");

	// A mistake the migration carries over is reported where the author made it.
	try
	{
		YamlArchive::Read<Cargo>("version: 1\nlabel: Halcyon ore\nmass: lots\n", migrations);
		FAIL("'lots' is not a number");
	}
	catch (const FormatError &error)
	{
		CHECK(std::string{error.what()}.find("(line 3)") != std::string::npos);
	}
	CHECK_THROWS_AS(YamlArchive::Read<Cargo>("version: 4\nlabel: x\n", migrations), FormatVersionError);
	CHECK_THROWS_AS(
	    YamlArchive::Read<CargoV1>("version: 1\nlabel: x\n", CargoYamlMigrations()), std::logic_error);
}

TEST_CASE("Migration: bytes inside an archive are written after their count, and checked as read")
{
	const std::vector<std::byte> inner{std::byte{1}, std::byte{2}, std::byte{0xff}};
	BinaryWriter writer{"Outer"};
	writer.WriteBytes(inner);
	writer.WriteBytes({});
	const std::vector<std::byte> bytes = writer.Finish({1});

	BinaryReader reader{bytes, "Outer", {1}};
	CHECK(reader.ReadBytes() == inner);
	CHECK(reader.ReadBytes().empty());
	reader.End();

	// A count past the end is refused, not allocated for.
	for (std::size_t cut = 1; cut <= 7; ++cut)
	{
		BinaryReader truncated{std::span{bytes}.first(bytes.size() - cut), "Outer", {1}};
		CHECK_THROWS_AS(
		    {
			    (void)truncated.ReadBytes();
			    (void)truncated.ReadBytes();
		    },
		    FormatError);
	}
}

namespace
{
// A record's fields hold names (an enumerator, a nested class's fields) as indices into the
// archive's table, which a record shares and adds to.
enum class Grade
{
	Coarse,
	Fine,
	Assayed,
};
struct Assay
{
	Grade grade{Grade::Coarse};
	std::uint32_t purity{0};
};
struct Lot
{
	std::string label;
	Assay assay;
	std::uint32_t crates{0};
};

void RegisterLot()
{
	static const bool registered = []
	{
		using namespace Jettison::Reflect;
		RegisterEnum<Grade>("MigrationTests.Grade")
		    .Value<Grade::Coarse>("Coarse")
		    .Value<Grade::Fine>("Fine")
		    .Value<Grade::Assayed>("Assayed");
		RegisterType<Assay>("MigrationTests.Assay")
		    .Field<&Assay::grade>("grade", Attribute::Saved)
		    .Field<&Assay::purity>("purity", Attribute::Saved);
		RegisterType<Lot>("MigrationTests.Lot")
		    .Field<&Lot::label>("label", Attribute::Saved)
		    .Field<&Lot::assay>("assay", Attribute::Saved)
		    .Field<&Lot::crates>("crates", Attribute::Saved);
		return true;
	}();
	(void)registered;
}
} // namespace

TEST_CASE("BinaryRecord: a value's fields edited as bytes, then read as the type is now")
{
	RegisterLot();
	BinaryWriter writer{"MigrationTests.Lots"};
	writer.Write(Lot{"Halcyon copper", {Grade::Fine, 80}, 3});
	writer.Write(Lot{"Vesper tin", {Grade::Coarse, 40}, 9});
	const std::vector<std::byte> bytes = writer.Finish({1});

	BinaryReader reader{bytes, "MigrationTests.Lots", {1}};
	BinaryRecord first = reader.ReadRecord();
	BinaryRecord second = reader.ReadRecord();
	reader.End();
	CHECK(first.Fields().size() == 3);
	CHECK(first.Has("assay"));
	CHECK(first.Get<Assay>("assay").grade == Grade::Fine);

	// An enumerator the archive's table never held, and a nested class rewritten, in one record; the
	// other record, read after, still reads its own names from the table they share.
	first.Set("assay", Assay{Grade::Assayed, 95});
	second.Rename("crates", "count");
	second.Rename("count", "crates");
	Lot lot;
	first.ReadInto(lot);
	CHECK(lot.label == "Halcyon copper");
	CHECK(lot.assay.grade == Grade::Assayed);
	CHECK(lot.assay.purity == 95);
	CHECK(lot.crates == 3);
	Lot other;
	second.ReadInto(other);
	CHECK(other.assay.grade == Grade::Coarse);
	CHECK(other.crates == 9);

	// A field the type does not save is refused as any value's is, and a field left out keeps the
	// value's.
	second.Rename("crates", "boxes");
	CHECK_THROWS_WITH_AS(
	    second.ReadInto(other), doctest::Contains("'MigrationTests.Lot' has no saved field 'boxes'"),
	    FormatError);
	second.Remove("boxes");
	Lot kept;
	kept.crates = 12;
	second.ReadInto(kept);
	CHECK(kept.crates == 12);

	// A field's bytes read by the primitives, and a field the record does not have.
	BinaryReader label = first.Open("label");
	CHECK(label.ReadUnsigned(4) == 14);
	CHECK_THROWS_AS(first.Open("mass"), std::logic_error);
	CHECK_THROWS_AS(first.Remove("mass"), std::logic_error);
	CHECK_THROWS_AS(first.Rename("label", "crates"), std::logic_error);
}
