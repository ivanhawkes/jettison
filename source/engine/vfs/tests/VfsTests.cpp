#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using Jettison::Vfs::FileSystem;

namespace
{
// A base, a mod and a user directory of their own per test case, removed when the fixture goes.
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-vfs-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory / "base");
		std::filesystem::create_directories(m_directory / "mod");
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

	// Base, then mod, then the writable user mount.
	FileSystem Mounted() const
	{
		FileSystem fileSystem;
		fileSystem.AddMount({"base", Path("base"), false});
		fileSystem.AddMount({"mod", Path("mod"), false});
		fileSystem.AddMount({"user", Path("user"), true});
		return fileSystem;
	}

  private:
	std::filesystem::path m_directory;
};

std::string Text(const std::optional<std::vector<std::byte>> &bytes)
{
	REQUIRE(bytes.has_value());
	return {reinterpret_cast<const char *>(bytes->data()), bytes->size()};
}

std::vector<std::byte> Bytes(std::string_view text)
{
	const auto *first = reinterpret_cast<const std::byte *>(text.data());
	return {first, first + text.size()};
}
} // namespace

TEST_CASE("Vfs: virtual paths are relative, forward-slashed, and cannot leave a mount")
{
	CHECK(FileSystem::IsValidPath("models/Fox.glb"));
	CHECK(FileSystem::IsValidPath("Fox.glb"));
	CHECK(FileSystem::IsValidPath(".gitkeep"));
	CHECK(FileSystem::IsValidPath("a/b c/d...e"));
	CHECK(FileSystem::IsValidPath("ünïcode/名前.yaml"));

	for (const char *invalid :
	     {"", "/models/Fox.glb", "models/", "models//Fox.glb", "../Fox.glb", "models/../../Fox.glb",
	      "./Fox.glb", "models/.", "models\\Fox.glb", "C:/Fox.glb", "Fox.glb:partial", "tab\there",
	      "del\x7f"})
	{
		CAPTURE(invalid);
		CHECK_FALSE(FileSystem::IsValidPath(invalid));
	}
}

TEST_CASE("Vfs: a later mount shadows an earlier one path by path")
{
	const Scratch scratch;
	scratch.Put("base/models/Fox.glb", "base fox");
	scratch.Put("base/models/Avocado.glb", "base avocado");
	scratch.Put("mod/models/Fox.glb", "mod fox");
	const FileSystem fileSystem = scratch.Mounted();

	CHECK(Text(fileSystem.Read("models/Fox.glb")) == "mod fox");
	CHECK(Text(fileSystem.Read("models/Avocado.glb")) == "base avocado");
	REQUIRE(fileSystem.Find("models/Fox.glb"));
	CHECK(fileSystem.Find("models/Fox.glb")->mount == 1);
	CHECK(fileSystem.Find("models/Fox.glb")->size == 7);
	CHECK(fileSystem.Find("models/Avocado.glb")->mount == 0);
	CHECK_FALSE(fileSystem.Find("models/Missing.glb"));
	CHECK(fileSystem.Exists("models/Avocado.glb"));
	CHECK_FALSE(fileSystem.Exists("models/Missing.glb"));
	CHECK_FALSE(fileSystem.Read("models/Missing.glb"));
	// A directory is not a file.
	CHECK_FALSE(fileSystem.Exists("models"));
	// An empty file is a file, read as no bytes.
	scratch.Put("base/empty.txt", "");
	CHECK(fileSystem.Read("empty.txt")->empty());
}

TEST_CASE("Vfs: an invalid path throws rather than reading as absent")
{
	const Scratch scratch;
	scratch.Put("secret.txt", "outside every mount");
	const FileSystem fileSystem = scratch.Mounted();
	CHECK_THROWS_AS((void)fileSystem.Exists("../secret.txt"), std::invalid_argument);
	CHECK_THROWS_AS((void)fileSystem.Read("../secret.txt"), std::invalid_argument);
	CHECK_THROWS_AS((void)fileSystem.Find("/etc/passwd"), std::invalid_argument);
	CHECK_THROWS_AS((void)fileSystem.ReadLayers("/etc/passwd"), std::invalid_argument);
	CHECK_THROWS_AS(fileSystem.Write("../secret.txt", Bytes("x")), std::invalid_argument);
	CHECK_THROWS_AS((void)fileSystem.Enumerate("models/"), std::invalid_argument);
}

TEST_CASE("Vfs: every mount's file at a path is read as a layer, the bottom mount's first")
{
	const Scratch scratch;
	scratch.Put("base/defs/goods/beer.yaml", "base beer");
	scratch.Put("mod/defs/goods/beer.yaml", "mod beer");
	scratch.Put("mod/defs/goods/wine.yaml", "mod wine");
	FileSystem fileSystem = scratch.Mounted();
	fileSystem.Write("defs/goods/beer.yaml", Bytes("user beer"));

	const std::vector<Jettison::Vfs::Layer> beer = fileSystem.ReadLayers("defs/goods/beer.yaml");
	REQUIRE(beer.size() == 3);
	CHECK(beer[0].mount == 0);
	CHECK(Text(beer[0].bytes) == "base beer");
	CHECK(beer[1].mount == 1);
	CHECK(Text(beer[1].bytes) == "mod beer");
	CHECK(beer[2].mount == 2);
	CHECK(Text(beer[2].bytes) == "user beer");
	const std::vector<Jettison::Vfs::Layer> wine = fileSystem.ReadLayers("defs/goods/wine.yaml");
	REQUIRE(wine.size() == 1);
	CHECK(wine[0].mount == 1);
	CHECK(fileSystem.ReadLayers("defs/goods/mead.yaml").empty());
}

TEST_CASE("Vfs: writes go to the user mount, replace a file whole, and shadow the base")
{
	const Scratch scratch;
	scratch.Put("base/settings.yaml", "base settings");
	const FileSystem fileSystem = scratch.Mounted();

	fileSystem.Write("settings.yaml", Bytes("user settings"));
	CHECK(Text(fileSystem.Read("settings.yaml")) == "user settings");
	CHECK(fileSystem.Find("settings.yaml")->mount == 2);

	// Directories are made as needed, and a second write replaces the first.
	fileSystem.Write("saves/slot1.bin", Bytes("first, and longer"));
	fileSystem.Write("saves/slot1.bin", Bytes("second"));
	CHECK(Text(fileSystem.Read("saves/slot1.bin")) == "second");
	// The partial file was renamed away.
	CHECK_FALSE(std::filesystem::exists(scratch.Path("user/saves/slot1.bin:partial")));
	// The base file is untouched.
	std::ifstream base{scratch.Path("base/settings.yaml")};
	CHECK(std::string{std::istreambuf_iterator<char>{base}, {}} == "base settings");
}

TEST_CASE("Vfs: enumeration lists every file under a directory once, sorted, across mounts")
{
	const Scratch scratch;
	scratch.Put("base/defs/ships/hauler.yaml", "");
	scratch.Put("base/defs/ships/fighter.yaml", "");
	scratch.Put("base/defs/goods.yaml", "");
	scratch.Put("mod/defs/ships/hauler.yaml", "");
	scratch.Put("mod/defs/ships/racer.yaml", "");
	scratch.Put("mod/other/readme.txt", "");
	FileSystem fileSystem = scratch.Mounted();
	fileSystem.Write("defs/ships/custom.yaml", Bytes(""));
	// An interrupted write leaves a partial file, which is not listed.
	scratch.Put("user/defs/ships/broken.yaml:partial", "");

	CHECK(
	    fileSystem.Enumerate("defs") == std::vector<std::string>{
	                                        "defs/goods.yaml", "defs/ships/custom.yaml",
	                                        "defs/ships/fighter.yaml", "defs/ships/hauler.yaml",
	                                        "defs/ships/racer.yaml"});
	CHECK(fileSystem.Enumerate("defs/ships").size() == 4);
	CHECK(fileSystem.Enumerate("").size() == 6);
	CHECK(fileSystem.Enumerate("nowhere").empty());
	CHECK_FALSE(fileSystem.Exists("defs/ships/broken.yaml"));
}

TEST_CASE("Vfs: mounts are checked as they are added")
{
	const Scratch scratch;
	FileSystem fileSystem;
	CHECK_THROWS_AS(fileSystem.AddMount({"", scratch.Path("base"), false}), std::invalid_argument);
	CHECK_THROWS_AS(
	    fileSystem.AddMount({"missing", scratch.Path("missing"), false}), std::runtime_error);
	// A file mounts as a pack, and one that is not a pack is refused, naming the mount.
	scratch.Put("notes.txt", "not a pack");
	CHECK_THROWS_WITH_AS(
	    fileSystem.AddMount({"notes", scratch.Path("notes.txt"), false}),
	    doctest::Contains("the mount 'notes' cannot be read"), std::runtime_error);
	CHECK(fileSystem.Mounts().empty());

	fileSystem.AddMount({"base", scratch.Path("base"), false});
	CHECK_THROWS_AS(fileSystem.AddMount({"base", scratch.Path("mod"), false}), std::invalid_argument);
	// No writable mount yet.
	CHECK_THROWS_AS(fileSystem.Write("save.bin", Bytes("x")), std::logic_error);

	// The writable mount is created, and nothing is mounted after it.
	fileSystem.AddMount({"user", scratch.Path("user/nested"), true});
	CHECK(std::filesystem::is_directory(scratch.Path("user/nested")));
	CHECK_THROWS_WITH_AS(
	    fileSystem.AddMount({"late", scratch.Path("mod"), false}),
	    doctest::Contains("after the writable mount 'user'"), std::invalid_argument);
	REQUIRE(fileSystem.Mounts().size() == 2);
	CHECK(fileSystem.Mounts()[1].source.is_absolute());
}
