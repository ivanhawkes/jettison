#include <jettison/sim/DefinitionRegistry.h>

#include <jettison/reflect/Reflect.h>
#include <jettison/serialise/Archive.h>
#include <jettison/serialise/Migration.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Jettison;
using Reflect::Attribute;

namespace
{
enum class Role
{
	Hauler,
	Fighter,
};

struct ShipClass
{
	std::string displayName;
	double mass{0.0};
	std::uint32_t cargo{0};
	Role role{Role::Hauler};
};

struct Unreflected
{
};

void RegisterTypes()
{
	static const bool registered = []
	{
		using namespace Reflect;
		RegisterEnum<Role>("DefinitionTests.Role")
		    .Value<Role::Hauler>("Hauler")
		    .Value<Role::Fighter>("Fighter");
		RegisterType<ShipClass>("DefinitionTests.ShipClass", Attribute::Moddable)
		    .Field<&ShipClass::displayName>("displayName", Attribute::Saved)
		    .Field<&ShipClass::mass>("mass", Attribute::Saved)
		    .Field<&ShipClass::cargo>("cargo", Attribute::Saved)
		    .Field<&ShipClass::role>("role", Attribute::Saved);
		return true;
	}();
	(void)registered;
}

// A base and a mod mount of their own per test case, removed when the fixture goes.
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-definition-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory / "base");
		std::filesystem::create_directories(m_directory / "mod");
		m_fileSystem.AddMount({"base", m_directory / "base", false});
		m_fileSystem.AddMount({"mod", m_directory / "mod", false});
	}
	~Scratch()
	{
		std::error_code error;
		std::filesystem::remove_all(m_directory, error);
	}
	Scratch(const Scratch &) = delete;
	Scratch &operator=(const Scratch &) = delete;

	void Put(const std::string &path, const std::string &contents) const
	{
		const auto file = m_directory / path;
		std::filesystem::create_directories(file.parent_path());
		std::ofstream{file, std::ios::binary | std::ios::trunc} << contents;
	}
	const Vfs::FileSystem &Files() const
	{
		return m_fileSystem;
	}

  private:
	std::filesystem::path m_directory;
	Vfs::FileSystem m_fileSystem;
};

void PutBase(const Scratch &scratch)
{
	scratch.Put(
	    "base/defs/ships/hauler.yaml", "version: 1\ndisplayName: Hauler\nmass: 4.5e5\ncargo: 800\n");
	scratch.Put(
	    "base/defs/ships/fighter.yaml",
	    "version: 1\ndisplayName: Fighter\nmass: 1.2e4\nrole: Fighter\n");
	scratch.Put(
	    "base/defs/ships/military/corvette.yaml", "version: 1\ndisplayName: Corvette\nrole: Fighter\n");
	scratch.Put("base/defs/ships/README.txt", "Not a definition.");
}
} // namespace

TEST_CASE("Definitions: every file under a directory is a definition, named by its path")
{
	RegisterTypes();
	const Scratch scratch;
	PutBase(scratch);
	Sim::DefinitionRegistry registry{scratch.Files()};
	registry.Load<ShipClass>("defs/ships", {1});

	CHECK(
	    registry.Names<ShipClass>() ==
	    std::vector<std::string>{"fighter", "hauler", "military/corvette"});
	CHECK(registry.Count() == 3);
	const auto *hauler = registry.Find<ShipClass>("hauler");
	REQUIRE(hauler != nullptr);
	CHECK(hauler->displayName == "Hauler");
	CHECK(hauler->mass == 4.5e5);
	CHECK(hauler->cargo == 800);
	CHECK(hauler->role == Role::Hauler);
	// Left out, so the default.
	CHECK(registry.Find<ShipClass>("military/corvette")->cargo == 0);
	CHECK(registry.Find<ShipClass>("military/corvette")->role == Role::Fighter);
	CHECK(registry.Find<ShipClass>("corvette") == nullptr);
	CHECK(registry.Find<ShipClass>("README") == nullptr);
}

TEST_CASE("Definitions: a mod overrides a definition by its path, and adds new ones")
{
	RegisterTypes();
	const Scratch scratch;
	PutBase(scratch);
	scratch.Put(
	    "mod/defs/ships/hauler.yaml",
	    "version: 1\ndisplayName: Heavy Hauler\nmass: 9e5\ncargo: 2000\n");
	scratch.Put("mod/defs/ships/racer.yaml", "version: 1\ndisplayName: Racer\nmass: 3000\n");
	Sim::DefinitionRegistry registry{scratch.Files()};
	registry.Load<ShipClass>("defs/ships", {1});

	CHECK(
	    registry.Names<ShipClass>() ==
	    std::vector<std::string>{"fighter", "hauler", "military/corvette", "racer"});
	// The whole definition is the mod's: nothing of the base's hauler is left.
	CHECK(registry.Find<ShipClass>("hauler")->displayName == "Heavy Hauler");
	CHECK(registry.Find<ShipClass>("hauler")->cargo == 2000);
	CHECK(registry.Find<ShipClass>("racer")->mass == 3000.0);
}

TEST_CASE("Definitions: a bad file is refused naming it, and the previous set stays")
{
	RegisterTypes();
	const Scratch scratch;
	PutBase(scratch);
	Sim::DefinitionRegistry registry{scratch.Files()};
	registry.Load<ShipClass>("defs/ships", {1});

	scratch.Put("mod/defs/ships/fighter.yaml", "version: 1\ndisplayName: Fighter\ncargo: lots\n");
	CHECK_THROWS_WITH_AS(
	    registry.Load<ShipClass>("defs/ships", {1}),
	    doctest::Contains(
	        "Definitions: defs/ships/fighter.yaml: YamlArchive: "
	        "DefinitionTests.ShipClass.cargo: expected a whole number"),
	    Serialise::FormatError);
	CHECK(registry.Find<ShipClass>("fighter")->mass == 1.2e4);

	scratch.Put("mod/defs/ships/fighter.yaml", "version: 2\ndisplayName: Fighter\n");
	CHECK_THROWS_WITH_AS(
	    registry.Load<ShipClass>("defs/ships", {1}),
	    doctest::Contains("defs/ships/fighter.yaml: DefinitionTests.ShipClass: version 2 found"),
	    Serialise::FormatError);
	CHECK(registry.Count() == 3);
}

TEST_CASE("Definitions: a file of an earlier version is migrated, so an old mod still loads")
{
	RegisterTypes();
	const Scratch scratch;
	PutBase(scratch);
	// Version 2 gives a mass in kilograms where version 1 gave tonnes. The base was written at
	// version 1, and a mod at 2.
	scratch.Put("mod/defs/ships/racer.yaml", "version: 2\ndisplayName: Racer\nmass: 3000\n");
	Serialise::YamlMigrations migrations{"DefinitionTests.ShipClass", {2}};
	migrations.Add(
	    1,
	    [](YAML::Node &root)
	    {
		    if (root["mass"])
			    root["mass"] = root["mass"].as<double>() * 1000.0;
	    });
	Sim::DefinitionRegistry registry{scratch.Files()};
	registry.Load<ShipClass>("defs/ships", migrations);
	CHECK(registry.Find<ShipClass>("hauler")->mass == 4.5e8);
	CHECK(registry.Find<ShipClass>("racer")->mass == 3000.0);
	CHECK(registry.Find<ShipClass>("military/corvette")->mass == 0.0);

	scratch.Put("mod/defs/ships/racer.yaml", "version: 3\ndisplayName: Racer\n");
	CHECK_THROWS_WITH_AS(
	    registry.Load<ShipClass>("defs/ships", migrations),
	    doctest::Contains(
	        "defs/ships/racer.yaml: DefinitionTests.ShipClass: version 3 found, version "
	        "2 expected: it was written by a later build"),
	    Serialise::FormatError);
	CHECK(registry.Find<ShipClass>("racer")->mass == 3000.0);
}

TEST_CASE("Definitions: an empty directory is an empty set, and an unreflected type is a defect")
{
	RegisterTypes();
	const Scratch scratch;
	Sim::DefinitionRegistry registry{scratch.Files()};
	registry.Load<ShipClass>("defs/ships", {1});
	CHECK(registry.Names<ShipClass>().empty());
	CHECK(registry.Find<ShipClass>("hauler") == nullptr);
	CHECK_THROWS_AS(registry.Load<Unreflected>("defs", {1}), Serialise::UnsupportedTypeError);
	CHECK(registry.Find<Unreflected>("anything") == nullptr);
}
