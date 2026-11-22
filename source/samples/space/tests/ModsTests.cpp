#include "Trading.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>

#include "Stations.h"
#include <jettison/app/Mounts.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/space/Cruise.h>
#include <jettison/vfs/FileSystem.h>
#include <jettison/vfs/Pack.h>

using namespace Jettison;

namespace
{
// A directory of its own under the temporary one, removed with it.
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-mods-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory);
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
		std::ofstream{file, std::ios::binary} << contents;
	}


	std::filesystem::path Path(const std::string &name) const
	{
		return m_directory / name;
	}

  private:
	std::filesystem::path m_directory;
};


} // namespace


TEST_CASE("Mounts: a data mod in the user's directory changes the game's definitions over a pack")
{
	const Scratch scratch;
	// The sample game's definitions, packed as a shipped build has them.
	Vfs::WritePack(
	    JETTISON_SAMPLE_ASSET_DIR, scratch.Path("assets.pack"), {.level = 19, .directories = {"defs"}});
	// A mod that makes beer dearer, and has Ostra Highport's market want more of it and deal in
	// wine, a good it adds.
	scratch.Put("user/mods/beer-and-wine/defs/goods/beer.patch.yaml", "version: 1\nbasePrice: 1500\n");
	scratch.Put(
	    "user/mods/beer-and-wine/defs/goods/wine.yaml",
	    "version: 1\ndisplayName: Wine\ncategory: CategoryFerments\nage: AgeIron\n"
	    "crate: a kilolitre of wine in casks\nbasePrice: 4000\n");
	scratch.Put(
	    "user/mods/beer-and-wine/defs/markets/ostra-highport.patch.yaml",
	    "version: 1\ngoods: !merge=good\n  - {good: beer, target: 60}\n"
	    "  - {good: wine, stock: 10, production: 0, consumption: 1, target: 20}\n");

	Vfs::FileSystem files;
	App::MountAssets(
	    files, JETTISON_ASSET_SOURCE_DIR, scratch.Path("assets.pack"), scratch.Path("user"));
	Sim::DefinitionRegistry definitions{files};
	Sample::LoadTradeDefinitions(definitions);

	CHECK(definitions.Names<Sample::Good>().size() == 9);
	const auto *beer = definitions.Find<Sample::Good>("beer");
	REQUIRE(beer != nullptr);
	CHECK(beer->basePrice == 1500);
	CHECK(beer->displayName == "Beer");
	CHECK(definitions.Find<Sample::Good>("wine")->basePrice == 4000);

	const auto *highport = definitions.Find<Sample::MarketDefinition>("ostra-highport");
	REQUIRE(highport != nullptr);
	REQUIRE(highport->goods.size() == 9);
	CHECK(highport->goods[2].good == "beer");
	CHECK(highport->goods[2].stock == 55);
	CHECK(highport->goods[2].target == 60);
	CHECK(highport->goods[8].good == "wine");
	// The other market is the base's.
	CHECK(definitions.Find<Sample::MarketDefinition>("corva-refinery")->goods.size() == 8);
}


TEST_CASE("Mounts: a mod gives a hull's drive a profile that coasts for most of the cruise")
{
	const Scratch scratch;
	// The shuttle at 1 g, thrusting for a quarter of each cruise.
	scratch.Put(
	    "user/mods/coasting/defs/drives/shuttle.patch.yaml",
	    "version: 1\nprofile:\n  boostAcceleration: 9.80665\n  boostSeconds: 0\n"
	    "  sustainedAcceleration: 9.80665\n  thrustShare: 0.25\n");
	Vfs::FileSystem files;
	App::MountAssets(files, JETTISON_ASSET_SOURCE_DIR, JETTISON_SAMPLE_ASSET_DIR, scratch.Path("user"));
	Sim::DefinitionRegistry definitions{files};
	Sample::LoadStationDefinitions(definitions);
	const Sample::Drives drives = Sample::ReadDrives(definitions);
	const Space::CruiseDrive &shuttle = drives.at("shuttle");
	REQUIRE(shuttle.profile);
	CHECK(shuttle.profile->thrustShare == 0.25f);
	CHECK(shuttle.seconds == 40.0f);
	const Space::CruiseThrustSeconds thrust = Space::CruiseThrust(*shuttle.profile, 3.96e11);
	CHECK(2.0 * thrust.sustained / Space::CruiseSeconds(shuttle, 3.96e11) == doctest::Approx(0.25));
	// The others are the base's.
	CHECK(!drives.at("liner").profile);

	// A share past the whole cruise is refused as the drive loads.
	scratch.Put(
	    "user/mods/coasting/defs/drives/shuttle.patch.yaml",
	    "version: 1\nprofile:\n  boostAcceleration: 9.80665\n  sustainedAcceleration: 9.80665\n"
	    "  thrustShare: 1.5\n");
	Sim::DefinitionRegistry refused{files};
	CHECK_THROWS_AS(Sample::LoadStationDefinitions(refused), Serialise::FormatError);
}
