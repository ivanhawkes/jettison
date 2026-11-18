#include <jettison/app/Mounts.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <jettison/core/Log.h>
#include <jettison/serialise/Archive.h>
#include <jettison/sim/DefinitionRegistry.h>
#include <jettison/sim/SaveGame.h>
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


TEST_CASE("Mounts: the engine's assets, then the user's mods by their identifiers, then the user")
{
	const Scratch scratch;
	scratch.Put("assets/defs/goods/beer.yaml", "version: 1\n");
	scratch.Put("user/mods/b-directory/defs/goods/wine.yaml", "version: 1\n");
	scratch.Put("mod-source/defs/goods/mead.yaml", "version: 1\n");
	Vfs::WritePack(scratch.Path("mod-source"), scratch.Path("user/mods/a-pack.pack"));
	scratch.Put("user/mods/readme.txt", "not a mod");
	scratch.Put("user/mods/c-damaged.pack", "not a pack either");
	// A directory and a pack of one identifier: the directory is the mod.
	scratch.Put("user/mods/a-pack/defs/goods/ale.yaml", "version: 1\n");

	std::vector<std::string> said;
	const Core::LogCallbackId listening = Core::AddLogCallback(
	    [&said](const Core::LogRecord &record)
	    {
		    if (record.category == "mounts" && record.severity >= Core::Severity::Warning)
			    said.emplace_back(record.message);
	    });
	Vfs::FileSystem files;
	App::MountAssets(files, scratch.Path("assets"), {}, scratch.Path("user"));
	Core::RemoveLogCallback(listening);

	CHECK(
	    NamesOf(files) == std::vector<std::string>{"engine", "mod:a-pack", "mod:b-directory", "user"});
	CHECK(files.Mounts().back().writable);
	CHECK(files.Find("defs/goods/ale.yaml")->mount == 1);
	CHECK_FALSE(files.Find("defs/goods/mead.yaml"));
	CHECK(files.Find("defs/goods/wine.yaml")->mount == 2);
	// The second a-pack, the damaged pack and the stray file are reported, and the rest still mount.
	REQUIRE(said.size() == 3);
	CHECK(said[0].find("readme.txt is not a mod") != std::string::npos);
	CHECK(said[1].find("a-pack.pack is a second mod a-pack, and is ignored") != std::string::npos);
	CHECK(said[2].find("c-damaged.pack is left out") != std::string::npos);

	// No mods directory is no mods.
	Vfs::FileSystem plain;
	App::MountAssets(plain, scratch.Path("assets"), {}, scratch.Path("elsewhere"));
	CHECK(NamesOf(plain) == std::vector<std::string>{"engine", "user"});
}


namespace
{
// The mounts' warnings and errors while it lives.
class Reported
{
  public:
	Reported()
	    : m_id{Core::AddLogCallback(
	          [this](const Core::LogRecord &record)
	          {
		          if (record.category == "mounts" && record.severity >= Core::Severity::Warning)
			          lines.emplace_back(record.message);
	          })}
	{
	}


	~Reported()
	{
		Core::RemoveLogCallback(m_id);
	}


	Reported(const Reported &) = delete;


	Reported &operator=(const Reported &) = delete;


	bool Contains(std::string_view text) const
	{
		return std::ranges::any_of(
		    lines, [text](const std::string &line) { return line.find(text) != std::string::npos; });
	}

	std::vector<std::string> lines;

  private:
	Core::LogCallbackId m_id;
};


std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary};
	return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
} // namespace


TEST_CASE("Mounts: the player's list orders the mods and enables them, and a mod found is added to it")
{
	const Scratch scratch;
	scratch.Put("assets/defs/goods/beer.yaml", "version: 1\n");
	scratch.Put("user/mods/alpha/mod.yaml", "version: 1\nname: Alpha\nrelease: 2.1.0\n");
	scratch.Put("user/mods/beta/defs/goods/beer.yaml", "version: 1\n");
	scratch.Put("user/mods/gamma/defs/goods/beer.yaml", "version: 1\n");
	scratch.Put("user/mods/delta/mod.yaml", "version: 1\nname: Delta\n");
	// Gamma before alpha, beta disabled, and one the player has since taken out of the directory.
	const std::filesystem::path list = scratch.Path("config/mods.yaml");
	scratch.Put(
	    "config/mods.yaml", "version: 1\nmods:\n  - {mod: gamma}\n  - {mod: gone}\n  - {mod: alpha}\n"
	                        "  - {mod: beta, enabled: false}\n");

	const Reported reported;
	Vfs::FileSystem files;
	const std::vector<Sim::ModVersion> mounted =
	    App::MountAssets(files, scratch.Path("assets"), {}, scratch.Path("user"), list);
	CHECK(
	    NamesOf(files) ==
	    std::vector<std::string>{"engine", "mod:gamma", "mod:alpha", "mod:delta", "user"});
	CHECK(mounted == std::vector<Sim::ModVersion>{{"gamma", ""}, {"alpha", "2.1.0"}, {"delta", ""}});
	CHECK(reported.Contains("The mod gone is in the list and not in"));
	CHECK(reported.lines.size() == 1);

	// Delta, found and not listed, is added at the end, enabled, and the missing mod keeps its place.
	App::RegisterModTypes();
	const auto written = Serialise::YamlArchive::Read<App::ModList>(ReadFile(list), {1});
	REQUIRE(written.mods.size() == 5);
	CHECK(written.mods[1].mod == "gone");
	CHECK_FALSE(written.mods[3].enabled);
	CHECK(written.mods[4].mod == "delta");
	CHECK(written.mods[4].enabled);

	// Read again, the list is what the directory has, and nothing is written.
	const std::string before = ReadFile(list);
	Vfs::FileSystem again;
	CHECK(App::MountAssets(again, scratch.Path("assets"), {}, scratch.Path("user"), list) == mounted);
	CHECK(ReadFile(list) == before);

	// A list that does not read is said, every mod mounts by name, and the player's file is kept.
	scratch.Put("config/mods.yaml", "version: 1\nmods: [{mod: alpha}, {mod: alpha}]\n");
	const Reported broken;
	Vfs::FileSystem unlisted;
	App::MountAssets(unlisted, scratch.Path("assets"), {}, scratch.Path("user"), list);
	CHECK(broken.Contains("names the mod 'alpha' twice"));
	CHECK(
	    NamesOf(unlisted) ==
	    std::vector<std::string>{"engine", "mod:alpha", "mod:beta", "mod:delta", "mod:gamma", "user"});
	CHECK(ReadFile(list) == "version: 1\nmods: [{mod: alpha}, {mod: alpha}]\n");
}


TEST_CASE("Mounts: a mod for a newer engine, or one whose requirement is not before it, is left out")
{
	const Scratch scratch;
	scratch.Put("assets/defs/goods/beer.yaml", "version: 1\n");
	scratch.Put("user/mods/base-goods/mod.yaml", "version: 1\nrelease: 1.0.0\nengine: 0.1.0\n");
	scratch.Put("user/mods/future/mod.yaml", "version: 1\nengine: 0.2.0\n");
	scratch.Put("user/mods/needs-base/mod.yaml", "version: 1\nrequires: [base-goods]\n");
	scratch.Put("user/mods/needs-future/mod.yaml", "version: 1\nrequires: [future]\n");
	scratch.Put("user/mods/needs-later/mod.yaml", "version: 1\nrequires: [zz-later]\n");
	scratch.Put("user/mods/zz-later/mod.yaml", "version: 1\n");
	scratch.Put("user/mods/misspelt/mod.yaml", "version: 1\nrelase: 1.0.0\n");

	const Reported reported;
	Vfs::FileSystem files;
	const std::vector<Sim::ModVersion> mounted =
	    App::MountAssets(files, scratch.Path("assets"), {}, scratch.Path("user"));
	CHECK(
	    NamesOf(files) ==
	    std::vector<std::string>{"engine", "mod:base-goods", "mod:needs-base", "mod:zz-later", "user"});
	CHECK(mounted.front() == Sim::ModVersion{"base-goods", "1.0.0"});
	CHECK(reported.Contains("written for engine 0.2.0, newer than this build's 0.1.0"));
	// A mod whose requirement was left out is left out with it; one that would need reordering is not
	// reordered.
	CHECK(reported.Contains("requires the mod future, which is not mounted before it"));
	CHECK(reported.Contains("requires the mod zz-later, which is not mounted before it"));
	CHECK(reported.Contains("misspelt is left out"));
	CHECK(reported.lines.size() == 4);
}


TEST_CASE("Mounts: content over the engine's assets, refused for a newer engine or a missing package")
{
	const Scratch scratch;
	scratch.Put("engine/strings/en/numbers.yaml", "version: 1\n");
	scratch.Put("content/strings/en/numbers.yaml", "version: 1\n");
	scratch.Put("content/mod.yaml", "version: 1\nname: Sample\nrelease: 1.0.0\nengine: 0.1.0\n");
	scratch.Put("user/mods/a/strings/en/numbers.yaml", "version: 1\n");

	Vfs::FileSystem files;
	CHECK(
	    App::MountAssets(files, scratch.Path("engine"), scratch.Path("content"), scratch.Path("user"))
	        .size() == 1);
	CHECK(NamesOf(files) == std::vector<std::string>{"engine", "base", "mod:a", "user"});
	CHECK(files.ReadLayers("strings/en/numbers.yaml").size() == 3);

	// Content a game cannot run on stops the start-up, where a mod would be left out.
	scratch.Put("content/mod.yaml", "version: 1\nengine: 0.2.0\n");
	Vfs::FileSystem newer;
	CHECK_THROWS_WITH(
	    App::MountAssets(newer, scratch.Path("engine"), scratch.Path("content"), scratch.Path("user")),
	    doctest::Contains("written for engine 0.2.0"));

	// So does content requiring a package not mounted before it: only the engine's assets are.
	scratch.Put("content/mod.yaml", "version: 1\nrequires: [engine]\n");
	Vfs::FileSystem engine;
	CHECK_NOTHROW(
	    App::MountAssets(
	        engine, scratch.Path("engine"), scratch.Path("content"), scratch.Path("user")));
	scratch.Put("content/mod.yaml", "version: 1\nrequires: [missing-package]\n");
	Vfs::FileSystem missing;
	CHECK_THROWS_WITH(
	    App::MountAssets(
	        missing, scratch.Path("engine"), scratch.Path("content"), scratch.Path("user")),
	    doctest::Contains("requires the package missing-package, which is not mounted before it"));
}
