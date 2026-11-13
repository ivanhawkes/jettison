#include "../src/Mounts.h"
#include "../src/Trading.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <jettison/core/Log.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/vfs/FileSystem.h>
#include <jettison/vfs/Pack.h>

using namespace Jettison;

namespace
{
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-mounts-tests-" + std::to_string(getpid()))}
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

std::vector<std::string> NamesOf(const Vfs::FileSystem &files)
{
	std::vector<std::string> names;
	for (const Vfs::Mount &mount : files.Mounts())
		names.push_back(mount.name);
	return names;
}
} // namespace

TEST_CASE("Mounts: the base, then the user's mods in the order of their names, then the user")
{
	const Scratch scratch;
	scratch.Put("assets/defs/goods/beer.yaml", "version: 1\n");
	scratch.Put("user/mods/b-directory/defs/goods/wine.yaml", "version: 1\n");
	scratch.Put("mod-source/defs/goods/mead.yaml", "version: 1\n");
	Vfs::WritePack(scratch.Path("mod-source"), scratch.Path("user/mods/a-pack.pack"));
	scratch.Put("user/mods/readme.txt", "not a mod");
	scratch.Put("user/mods/c-damaged.pack", "not a pack either");

	std::vector<std::string> said;
	const Core::LogCallbackId listening = Core::AddLogCallback(
	    [&said](const Core::LogRecord &record)
	    {
		    if (record.category == "mounts" && record.severity >= Core::Severity::Warning)
			    said.emplace_back(record.message);
	    });
	Vfs::FileSystem files;
	App::MountAssets(files, scratch.Path("assets"), scratch.Path("user"));
	Core::RemoveLogCallback(listening);

	CHECK(
	    NamesOf(files) ==
	    std::vector<std::string>{"base", "mod:a-pack.pack", "mod:b-directory", "user"});
	CHECK(files.Mounts().back().writable);
	CHECK(files.Find("defs/goods/mead.yaml")->mount == 1);
	CHECK(files.Find("defs/goods/wine.yaml")->mount == 2);
	// The damaged pack and the stray file are reported, and the rest still mount.
	REQUIRE(said.size() == 2);
	CHECK(said[0].find("readme.txt is not a mod") != std::string::npos);
	CHECK(said[1].find("c-damaged.pack is left out") != std::string::npos);

	// No mods directory is no mods.
	Vfs::FileSystem plain;
	App::MountAssets(plain, scratch.Path("assets"), scratch.Path("elsewhere"));
	CHECK(NamesOf(plain) == std::vector<std::string>{"base", "user"});
}

TEST_CASE("Mounts: a data mod in the user's directory changes the game's definitions over a pack")
{
	const Scratch scratch;
	// The sample game's definitions, packed as a shipped build has them.
	Vfs::WritePack(
	    JETTISON_ASSET_SOURCE_DIR, scratch.Path("assets.pack"), {.level = 19, .directories = {"defs"}});
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
	App::MountAssets(files, scratch.Path("assets.pack"), scratch.Path("user"));
	Sim::DefinitionRegistry definitions{files};
	App::LoadTradeDefinitions(definitions);

	CHECK(definitions.Names<App::Good>().size() == 9);
	const auto *beer = definitions.Find<App::Good>("beer");
	REQUIRE(beer != nullptr);
	CHECK(beer->basePrice == 1500);
	CHECK(beer->displayName == "Beer");
	CHECK(definitions.Find<App::Good>("wine")->basePrice == 4000);

	const auto *highport = definitions.Find<App::MarketDefinition>("ostra-highport");
	REQUIRE(highport != nullptr);
	REQUIRE(highport->goods.size() == 9);
	CHECK(highport->goods[2].good == "beer");
	CHECK(highport->goods[2].stock == 55);
	CHECK(highport->goods[2].target == 60);
	CHECK(highport->goods[8].good == "wine");
	// The other market is the base's.
	CHECK(definitions.Find<App::MarketDefinition>("corva-refinery")->goods.size() == 8);
}
