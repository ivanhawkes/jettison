#include <jettison/core/File.h>
#include <jettison/core/FileLock.h>

#include <doctest/doctest.h>

#include <signal.h>
#include <sys/wait.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace Jettison::Core;

namespace
{
std::filesystem::path Scratch()
{
	const auto directory =
	    std::filesystem::temp_directory_path() / ("jettison-file-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(directory, error);
	return directory;
}


std::string ReadFile(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary};
	return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
} // namespace


TEST_CASE("File: a durable write makes its directory, replaces the file whole, and leaves no partial")
{
	const std::filesystem::path directory = Scratch();
	const std::filesystem::path path = directory / "saves" / "slot.save";
	WriteFileDurably(path, std::string_view{"first, and longer"});
	CHECK(ReadFile(path) == "first, and longer");
	WriteFileDurably(path, std::string_view{"second"});
	CHECK(ReadFile(path) == "second");
	CHECK(std::distance(std::filesystem::directory_iterator{path.parent_path()}, {}) == 1);

	// Nothing at all is a file too.
	WriteFileDurably(path, std::string_view{});
	CHECK(std::filesystem::file_size(path) == 0);
	std::filesystem::remove_all(directory);
}


TEST_CASE("File: a write that fails says where, and leaves the old file and no partial")
{
	const std::filesystem::path directory = Scratch();
	const std::filesystem::path path = directory / "slot.save";
	WriteFileDurably(path, std::string_view{"kept"});

	// A directory no partial file can be made in: the open fails.
	using std::filesystem::perms;
	std::filesystem::permissions(directory, perms::owner_read | perms::owner_exec);
	try
	{
		WriteFileDurably(path, std::string_view{"lost"});
		FAIL("the write should have failed");
	}
	catch (const std::runtime_error &error)
	{
		CHECK(std::string{error.what()}.find(path.string()) != std::string::npos);
	}
	std::filesystem::permissions(directory, perms::owner_all);
	CHECK(ReadFile(path) == "kept");
	CHECK(std::distance(std::filesystem::directory_iterator{directory}, {}) == 1);

	// A file where its directory would be.
	CHECK_THROWS_AS(
	    WriteFileDurably(path / "under-a-file.save", std::string_view{"x"}), std::runtime_error);
	CHECK(ReadFile(path) == "kept");
	std::filesystem::remove_all(directory);
}


TEST_CASE("File: directories are made to any depth, once, and not through a file")
{
	const std::filesystem::path directory = Scratch();
	const std::filesystem::path nested = directory / "worlds" / "first" / "journal";
	CHECK(CreateDirectoriesDurably(nested));
	CHECK(std::filesystem::is_directory(nested));
	CHECK_FALSE(CreateDirectoriesDurably(nested));
	CHECK_FALSE(CreateDirectoriesDurably(directory / "worlds" / "first" / ""));

	WriteFileDurably(directory / "file", std::string_view{"x"});
	CHECK_THROWS_AS(CreateDirectoriesDurably(directory / "file"), std::runtime_error);
	CHECK_THROWS_AS(CreateDirectoriesDurably(directory / "file" / "under"), std::runtime_error);
	std::filesystem::remove_all(directory);
}


TEST_CASE("File: directories are made with the permissions given, and one already there kept as it is")
{
	using std::filesystem::perms;
	const std::filesystem::path directory = Scratch();
	std::filesystem::create_directories(directory);
	constexpr perms kShared = perms::owner_all | perms::group_read | perms::group_exec;
	std::filesystem::permissions(directory, kShared);
	const auto mode = [](const std::filesystem::path &path)
	{ return std::filesystem::status(path).permissions() & perms::mask; };

	CHECK(CreateDirectoriesDurably(directory / "a" / "b", perms::owner_all));
	CHECK(mode(directory) == kShared);
	CHECK(mode(directory / "a") == perms::owner_all);
	CHECK(mode(directory / "a" / "b") == perms::owner_all);

	// Through one it makes and back out: the directory named so is there already, and kept.
	CHECK(CreateDirectoriesDurably(directory / "new" / ".." / "made", perms::owner_all));
	CHECK(mode(directory) == kShared);
	CHECK(mode(directory / "new") == perms::owner_all);
	CHECK(mode(directory / "made") == perms::owner_all);
	std::filesystem::remove_all(directory);
}


TEST_CASE("File: a file is written with the permissions given, over a file and a partial left open")
{
	using std::filesystem::perms;
	const std::filesystem::path directory = Scratch();
	const std::filesystem::path path = directory / "secret";
	constexpr perms kPrivate = perms::owner_read | perms::owner_write;
	const auto mode = [](const std::filesystem::path &file)
	{ return std::filesystem::status(file).permissions() & perms::mask; };

	WriteFileDurably(path, std::string_view{"open"}, kFilePermissions);
	std::filesystem::permissions(path, kFilePermissions);

	// Its partial is new, so made private, whatever is open to everyone beside it.
	WriteFileDurably(path, std::string_view{"private"}, kPrivate);
	CHECK(ReadFile(path) == "private");
	CHECK(mode(path) == kPrivate);
	std::filesystem::remove_all(directory);
}


TEST_CASE("File: partials writers that are gone left are removed, and nothing else is")
{
	// A process that has ended, whose ID nothing runs under once it is reaped.
	const ::pid_t gone = ::fork();
	REQUIRE(gone >= 0);
	if (gone == 0)
		::_exit(0);
	::waitpid(gone, nullptr, 0);
	const auto partialName = [](::pid_t writer)
	{ return std::format("{}.{}.{:016x}.0", kPartialPrefix, writer, 0); };
	const auto put = [](const std::filesystem::path &file)
	{
		std::ofstream{file} << "left";
		return file;
	};

	// Directories this process has not written into: each one's first write is the one that looks.
	const std::filesystem::path directory = Scratch() / "abandoned";
	std::filesystem::create_directories(directory);
	const std::filesystem::path abandoned = put(directory / partialName(gone));
	const std::filesystem::path running = put(directory / partialName(::getpid()));
	const std::filesystem::path notes = put(directory / "notes:partial");
	const std::filesystem::path shorter = put(directory / (partialName(gone) + "x"));

	// Names no writer gives, however near: a process group's negative ID, digits padded, a mark in
	// capitals.
	const std::vector<std::filesystem::path> near{
	    put(directory / std::format("{}.-{}.{:016x}.0", kPartialPrefix, gone, 0)),
	    put(directory / std::format("{}.0{}.{:016x}.0", kPartialPrefix, gone, 0)),
	    put(directory / std::format("{}.{}.{:016x}.00", kPartialPrefix, gone, 0)),
	    put(directory / std::format("{}.{}.{:016X}.0", kPartialPrefix, gone, 0xabcdefULL))};
	WriteFileDurably(directory / "slot.save", std::string_view{"written"});
	CHECK(ReadFile(directory / "slot.save") == "written");
	CHECK_FALSE(std::filesystem::exists(abandoned));
	CHECK(ReadFile(running) == "left");
	CHECK(ReadFile(notes) == "left");
	CHECK(ReadFile(shorter) == "left");
	for (const std::filesystem::path &kept : near)
	{
		CAPTURE(kept);
		CHECK(ReadFile(kept) == "left");
	}

	// A file named as a gone writer's partial is never taken for one when it is the file written.
	const std::filesystem::path own = Scratch() / "own";
	std::filesystem::create_directories(own);
	const std::filesystem::path target = put(own / partialName(gone));
	CHECK_THROWS_AS(WriteNewFileDurably(target, std::string_view{"over"}), FileExistsError);
	CHECK(ReadFile(target) == "left");
	std::filesystem::remove_all(directory.parent_path());
}


TEST_CASE("File: a file of a name as long as a file system allows is written, by both writes")
{
	// NAME_MAX on Linux's file systems, as the name a write's partial must not lengthen.
	constexpr std::size_t kLongestName = 255;
	const std::filesystem::path directory = Scratch();
	const std::filesystem::path path = directory / std::string(kLongestName, 'n');
	WriteFileDurably(path, std::string_view{"replaced"});
	CHECK(ReadFile(path) == "replaced");
	std::filesystem::remove(path);
	WriteNewFileDurably(path, std::string_view{"new"});
	CHECK(ReadFile(path) == "new");
	std::filesystem::remove_all(directory);
}


TEST_CASE("File: a new file is written, and one already there is refused and left as it was")
{
	const std::filesystem::path directory = Scratch();
	const std::filesystem::path path = directory / "new";
	WriteNewFileDurably(path, std::string_view{"first"});
	CHECK(ReadFile(path) == "first");

	try
	{
		WriteNewFileDurably(path, std::string_view{"second"});
		FAIL("the write over a file did not throw");
	}
	catch (const FileExistsError &refused)
	{
		CHECK(std::string{refused.what()}.find(path.string()) != std::string::npos);
	}
	CHECK(ReadFile(path) == "first");
	CHECK(std::distance(std::filesystem::directory_iterator{directory}, {}) == 1);

	// A directory is a file in the way too.
	std::filesystem::create_directories(directory / "dir");
	CHECK_THROWS_AS(WriteNewFileDurably(directory / "dir", std::string_view{"x"}), FileExistsError);
	CHECK(std::filesystem::is_directory(directory / "dir"));

	// The write that replaces still does.
	WriteFileDurably(path, std::string_view{"replaced"});
	CHECK(ReadFile(path) == "replaced");
	std::filesystem::remove_all(directory);
}


TEST_CASE("File: of writers making the same new file at once, one wins whole and the rest are refused")
{
	constexpr int kWriters = 8;
	constexpr int kRounds = 25;
	constexpr std::size_t kSize = 1 << 16;
	const std::filesystem::path directory = Scratch();
	for (int round = 0; round < kRounds; ++round)
	{
		const std::filesystem::path path = directory / ("new" + std::to_string(round));
		std::atomic<int> won{0};
		std::atomic<int> refused{0};
		std::vector<std::thread> writers;
		writers.reserve(kWriters);
		for (int writer = 0; writer < kWriters; ++writer)
			writers.emplace_back(
			    [&, writer]
			    {
				    const std::string text(kSize, static_cast<char>('a' + writer));
				    try
				    {
					    WriteNewFileDurably(path, text);
					    ++won;
				    }
				    catch (const FileExistsError &)
				    {
					    ++refused;
				    }
			    });
		for (std::thread &writer : writers)
			writer.join();
		CHECK(won == 1);
		CHECK(refused == kWriters - 1);
		const std::string content = ReadFile(path);
		CHECK(content == std::string(kSize, content.empty() ? '\0' : content.front()));
	}
	// Each round left its one file and no partial.
	CHECK(std::distance(std::filesystem::directory_iterator{directory}, {}) == kRounds);
	std::filesystem::remove_all(directory);
}


TEST_CASE("FileLock: one holder at a time in a process, and the next once it lets go")
{
	const std::filesystem::path directory = Scratch();
	std::filesystem::create_directories(directory);
	const std::filesystem::path path = directory / "lock";
	{
		std::optional<FileLock> held = FileLock::TryAcquire(path);
		REQUIRE(held);
		CHECK_FALSE(FileLock::TryAcquire(path));
		const FileLock moved = std::move(*held);
		held.reset();
		CHECK_FALSE(FileLock::TryAcquire(path));
	}
	CHECK(FileLock::TryAcquire(path));
	CHECK_THROWS_AS((void)FileLock::TryAcquire(directory / "missing" / "lock"), std::runtime_error);
	std::filesystem::remove_all(directory);
}


TEST_CASE("FileLock: a lock file that does not read, or says too much, gives what it can")
{
	const std::filesystem::path directory = Scratch();
	std::filesystem::create_directories(directory);
	CHECK(FileLock::HolderOf(directory / "missing").empty());
	std::ofstream{directory / "long"} << std::string(kHolderLimit * 2, 'x') << "\nsecond line\n";
	CHECK(FileLock::HolderOf(directory / "long") == std::string(kHolderLimit, 'x'));
	std::ofstream{directory / "short"} << "7 left\nsecond line\n";
	CHECK(FileLock::HolderOf(directory / "short") == "7 left");
	std::filesystem::remove_all(directory);
}


TEST_CASE("FileLock: another process is refused, and a killed holder's lock is free")
{
	const std::filesystem::path directory = Scratch();
	std::filesystem::create_directories(directory);
	const std::filesystem::path path = directory / "lock";
	std::array<int, 2> ready{};
	REQUIRE(::pipe(ready.data()) == 0);
	const pid_t holder = ::fork();
	REQUIRE(holder >= 0);
	if (holder == 0)
	{
		const std::optional<FileLock> held = FileLock::TryAcquire(path);
		const char answer = held ? 'y' : 'n';
		(void)::write(ready[1], &answer, 1);
		::pause();
		::_exit(0);
	}
	char answer = 0;
	REQUIRE(::read(ready[0], &answer, 1) == 1);
	CHECK(answer == 'y');
	CHECK_FALSE(FileLock::TryAcquire(path));

	// The refused caller reads the holder's ID and program name, here the same program's.
	const std::string described = std::format("{} {}", holder, program_invocation_short_name);
	CHECK(FileLock::HolderOf(path) == described);

	// No destructor runs in a killed process: the kernel lets go of the lock, and the file still says
	// who held it until the next holder writes its own over it.
	REQUIRE(::kill(holder, SIGKILL) == 0);
	REQUIRE(::waitpid(holder, nullptr, 0) == holder);
	CHECK(FileLock::HolderOf(path) == described);
	{
		const std::optional<FileLock> next = FileLock::TryAcquire(path);
		REQUIRE(next);
		CHECK(
		    FileLock::HolderOf(path) ==
		    std::format("{} {}", ::getpid(), program_invocation_short_name));
	}
	::close(ready[0]);
	::close(ready[1]);
	std::filesystem::remove_all(directory);
}
