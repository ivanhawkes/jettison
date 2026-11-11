#include <jettison/core/File.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

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
