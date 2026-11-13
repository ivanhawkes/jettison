#include <jettison/vfs/FileSystem.h>
#include <jettison/vfs/Pack.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

using Jettison::Vfs::FileSystem;
using Jettison::Vfs::PackSummary;
using Jettison::Vfs::WritePack;

namespace
{
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-pack-tests-" + std::to_string(getpid()))}
	{
		std::filesystem::remove_all(m_directory);
		std::filesystem::create_directories(m_directory / "assets");
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
	std::string Contents(const std::string &name) const
	{
		std::ifstream file{Path(name), std::ios::binary};
		return {std::istreambuf_iterator<char>{file}, {}};
	}

  private:
	std::filesystem::path m_directory;
};

std::string Text(const std::optional<std::vector<std::byte>> &bytes)
{
	REQUIRE(bytes.has_value());
	return {reinterpret_cast<const char *>(bytes->data()), bytes->size()};
}

// Text that compresses, and bytes that do not: a generator's output, the same every run.
std::string Repetitive(std::size_t size)
{
	std::string text;
	while (text.size() < size)
		text += "goods:\n  - {good: beer, stock: 55, production: 2, consumption: 1, target: 40}\n";
	text.resize(size);
	return text;
}

std::string Noise(std::size_t size)
{
	std::string bytes(size, '\0');
	std::uint64_t state = 0x9e3779b97f4a7c15ULL;
	for (char &byte : bytes)
	{
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		byte = static_cast<char>(state & 0xff);
	}
	return bytes;
}

// The sample of a base: text that compresses, noise that does not, an empty file and a nested one.
void PutBase(const Scratch &scratch)
{
	scratch.Put("assets/defs/goods/beer.yaml", Repetitive(4096));
	scratch.Put("assets/textures/noise.ktx2", Noise(8192));
	scratch.Put("assets/empty.txt", "");
	scratch.Put("assets/defs/ships/military/corvette.yaml", "version: 1\n");
	// A sibling whose name begins as the directory's does, which a listing of defs must leave out.
	scratch.Put("assets/defs2/other.yaml", "other");
}
} // namespace

TEST_CASE("Pack: a packed directory reads as the directory does")
{
	const Scratch scratch;
	PutBase(scratch);
	const PackSummary summary = WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	CHECK(summary.files == 5);
	CHECK(summary.skipped.empty());
	CHECK(summary.packedBytes == std::filesystem::file_size(scratch.Path("base.pack")));
	CHECK(summary.bytes == 4096 + 8192 + 11 + 5);

	FileSystem loose;
	loose.AddMount({"base", scratch.Path("assets")});
	FileSystem packed;
	packed.AddMount({"base", scratch.Path("base.pack")});

	for (const std::string directory : {"", "defs", "defs/ships", "textures", "nowhere"})
	{
		CAPTURE(directory);
		CHECK(packed.Enumerate(directory) == loose.Enumerate(directory));
	}
	CHECK(
	    packed.Enumerate("defs") ==
	    std::vector<std::string>{"defs/goods/beer.yaml", "defs/ships/military/corvette.yaml"});
	for (const std::string &path : loose.Enumerate(""))
	{
		CAPTURE(path);
		CHECK(Text(packed.Read(path)) == Text(loose.Read(path)));
		REQUIRE(packed.Find(path));
		CHECK(packed.Find(path)->size == loose.Find(path)->size);
	}
	CHECK(packed.Exists("defs/goods/beer.yaml"));
	CHECK_FALSE(packed.Exists("defs/goods"));
	CHECK_FALSE(packed.Read("defs/goods/wine.yaml"));
	CHECK(packed.Read("empty.txt")->empty());
	CHECK_THROWS_AS((void)packed.Read("../base.pack"), std::invalid_argument);
}

TEST_CASE("Pack: a file is compressed where that pays and stored where it does not")
{
	const Scratch scratch;
	PutBase(scratch);
	const PackSummary summary = WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	// The text alone: the noise does not shrink, and the small files are too small to.
	CHECK(summary.compressed == 1);
	CHECK(summary.packedBytes < summary.bytes);
	// The same files give the same bytes.
	WritePack(scratch.Path("assets"), scratch.Path("again.pack"));
	CHECK(scratch.Contents("base.pack") == scratch.Contents("again.pack"));
	// No partial file is left beside it.
	CHECK_FALSE(std::filesystem::exists(scratch.Path("base.pack.partial")));

	CHECK_THROWS_AS(
	    WritePack(scratch.Path("assets"), scratch.Path("bad.pack"), {.level = 0, .directories = {}}),
	    std::invalid_argument);
	CHECK_THROWS_AS(
	    WritePack(scratch.Path("assets"), scratch.Path("bad.pack"), {.level = 23, .directories = {}}),
	    std::invalid_argument);
	CHECK_THROWS_AS(WritePack(scratch.Path("missing"), scratch.Path("bad.pack")), std::runtime_error);
}

TEST_CASE("Pack: a file no virtual path could name is left out and reported")
{
	const Scratch scratch;
	scratch.Put("assets/good.yaml", "good");
	scratch.Put("assets/saves/slot:partial", "half a save");
	const PackSummary summary = WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	CHECK(summary.files == 1);
	CHECK(summary.skipped == std::vector<std::string>{"saves/slot:partial"});
}

TEST_CASE("Pack: the directories named are packed, each file once, and nothing beside them")
{
	const Scratch scratch;
	PutBase(scratch);
	scratch.Put("assets/CMakeFiles/rules.ninja", "the build system's");
	const PackSummary summary = WritePack(
	    scratch.Path("assets"), scratch.Path("base.pack"),
	    {.level = 19, .directories = {"defs", "textures", "defs/ships"}});
	CHECK(summary.files == 3);
	FileSystem packed;
	packed.AddMount({"base", scratch.Path("base.pack")});
	CHECK(
	    packed.Enumerate("") ==
	    std::vector<std::string>{
	        "defs/goods/beer.yaml", "defs/ships/military/corvette.yaml", "textures/noise.ktx2"});

	CHECK_THROWS_AS(
	    WritePack(
	        scratch.Path("assets"), scratch.Path("bad.pack"),
	        {.level = 19, .directories = {"../assets"}}),
	    std::invalid_argument);
	CHECK_THROWS_AS(
	    WritePack(
	        scratch.Path("assets"), scratch.Path("bad.pack"), {.level = 19, .directories = {"sounds"}}),
	    std::runtime_error);
}

TEST_CASE("Pack: an empty directory packs to a pack with nothing in it")
{
	const Scratch scratch;
	WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	FileSystem packed;
	packed.AddMount({"base", scratch.Path("base.pack")});
	CHECK(packed.Enumerate("").empty());
}

TEST_CASE("Pack: packs and directories layer as mounts, path by path")
{
	const Scratch scratch;
	PutBase(scratch);
	WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	scratch.Put("mod/defs/goods/beer.yaml", "a mod's beer");
	scratch.Put("mod/defs/goods/wine.yaml", "a mod's wine");
	scratch.Put("dlc/defs/goods/wine.yaml", "the dlc's wine");
	WritePack(scratch.Path("dlc"), scratch.Path("dlc.pack"));

	FileSystem files;
	files.AddMount({"base", scratch.Path("base.pack")});
	files.AddMount({"dlc", scratch.Path("dlc.pack")});
	files.AddMount({"mod", scratch.Path("mod")});
	files.AddMount({"user", scratch.Path("user"), true});

	CHECK(Text(files.Read("defs/goods/beer.yaml")) == "a mod's beer");
	CHECK(files.Find("defs/goods/beer.yaml")->mount == 2);
	CHECK(files.Find("defs/ships/military/corvette.yaml")->mount == 0);
	const std::vector<Jettison::Vfs::Layer> wine = files.ReadLayers("defs/goods/wine.yaml");
	REQUIRE(wine.size() == 2);
	CHECK(wine[0].mount == 1);
	CHECK(Text(wine[0].bytes) == "the dlc's wine");
	CHECK(wine[1].mount == 2);
	CHECK(
	    files.Enumerate("defs/goods") ==
	    std::vector<std::string>{"defs/goods/beer.yaml", "defs/goods/wine.yaml"});

	// A pack is read-only: a writable mount is a directory.
	FileSystem writable;
	CHECK_THROWS_AS(writable.AddMount({"user", scratch.Path("base.pack"), true}), std::runtime_error);
}

TEST_CASE("Pack: every prefix of a pack is refused as it is mounted")
{
	const Scratch scratch;
	PutBase(scratch);
	WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	const std::string whole = scratch.Contents("base.pack");
	for (std::size_t size = 0; size < whole.size(); ++size)
	{
		CAPTURE(size);
		scratch.Put("cut.pack", whole.substr(0, size));
		FileSystem files;
		CHECK_THROWS_AS(files.AddMount({"cut", scratch.Path("cut.pack")}), std::runtime_error);
	}
}

TEST_CASE("Pack: a damaged pack is refused, saying what is wrong")
{
	const Scratch scratch;
	PutBase(scratch);
	WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	const std::string whole = scratch.Contents("base.pack");
	const auto refused = [&scratch](const std::string &bytes, const char *problem)
	{
		CAPTURE(problem);
		scratch.Put("damaged.pack", bytes);
		FileSystem files;
		CHECK_THROWS_WITH_AS(
		    files.AddMount({"damaged", scratch.Path("damaged.pack")}), doctest::Contains(problem),
		    std::runtime_error);
	};

	std::string magic = whole;
	magic[0] = 'X';
	refused(magic, "does not begin as a pack does");
	std::string layout = whole;
	layout[8] = '\x02';
	refused(layout, "its layout is 2");
	// The index's first path, "defs/goods/beer.yaml", given a segment no virtual path has.
	std::string path = whole;
	path.replace(path.find("defs/goods/beer.yaml"), 4, "..//");
	refused(path, "not a virtual path");
	// Its second, "defs/ships/military/corvette.yaml", made to sort before the first.
	std::string order = whole;
	order[order.find("defs/ships/military")] = 'a';
	refused(order, "not sorted by path");
}

TEST_CASE("Pack: a damaged file in a pack is caught as it is read")
{
	const Scratch scratch;
	PutBase(scratch);
	WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	std::string bytes = scratch.Contents("base.pack");
	// The compressed text is the first file's bytes, straight after the 32-byte header; one in its
	// middle changed fails its checksum, or its decoding.
	bytes[32 + 14] = static_cast<char>(bytes[32 + 14] ^ 0x5a);
	scratch.Put("damaged.pack", bytes);

	FileSystem files;
	files.AddMount({"damaged", scratch.Path("damaged.pack")});
	CHECK_THROWS_WITH_AS(
	    (void)files.Read("defs/goods/beer.yaml"), doctest::Contains("defs/goods/beer.yaml"),
	    std::runtime_error);
	// The rest still read.
	CHECK(Text(files.Read("defs/ships/military/corvette.yaml")) == "version: 1\n");
}

TEST_CASE("Pack: a pack replaced on the disk while mounted is read as it was mounted")
{
	const Scratch scratch;
	scratch.Put("assets/note.txt", "first");
	WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	FileSystem files;
	files.AddMount({"base", scratch.Path("base.pack")});

	scratch.Put("assets/note.txt", "second, and at another size");
	scratch.Put("assets/aaa-first.txt", "moves every offset");
	WritePack(scratch.Path("assets"), scratch.Path("base.pack"));
	CHECK(Text(files.Read("note.txt")) == "first");
	CHECK_FALSE(files.Exists("aaa-first.txt"));
}
