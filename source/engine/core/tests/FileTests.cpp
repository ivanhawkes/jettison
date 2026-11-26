#include <jettison/core/File.h>
#include <jettison/core/FileLock.h>

#include <doctest/doctest.h>

#include <signal.h>
#include <sys/wait.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

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
	std::filesystem::path partial = path;
	partial += kPartialSuffix;
	CHECK_FALSE(std::filesystem::exists(partial));

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

	// A directory where the partial file would go: the open fails.
	std::filesystem::path partial = path;
	partial += kPartialSuffix;
	std::filesystem::create_directories(partial / "in-the-way");
	try
	{
		WriteFileDurably(path, std::string_view{"lost"});
		FAIL("the write should have failed");
	}
	catch (const std::runtime_error &error)
	{
		CHECK(std::string{error.what()}.find(path.string()) != std::string::npos);
	}
	CHECK(ReadFile(path) == "kept");
	std::filesystem::remove_all(partial);

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

	// A partial a crash left, open to everyone, is not reused.
	std::filesystem::path partial = path;
	partial += kPartialSuffix;
	std::ofstream{partial} << "left";
	std::filesystem::permissions(partial, kFilePermissions);
	WriteFileDurably(path, std::string_view{"private"}, kPrivate);
	CHECK(ReadFile(path) == "private");
	CHECK(mode(path) == kPrivate);
	CHECK_FALSE(std::filesystem::exists(partial));
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

	// No destructor runs in a killed process: the kernel lets go of the lock.
	REQUIRE(::kill(holder, SIGKILL) == 0);
	REQUIRE(::waitpid(holder, nullptr, 0) == holder);
	CHECK(FileLock::TryAcquire(path));
	::close(ready[0]);
	::close(ready[1]);
	std::filesystem::remove_all(directory);
}
