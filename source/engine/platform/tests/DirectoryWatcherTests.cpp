#include <jettison/platform/DirectoryWatcher.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Jettison::Platform;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace
{
struct Tree
{
	explicit Tree(const char *name) : root{fs::temp_directory_path() / "jettison-watcher-tests" / name}
	{
		fs::remove_all(root);
		fs::create_directories(root);
	}


	~Tree()
	{
		fs::remove_all(root);
	}


	Tree(const Tree &) = delete;


	Tree &operator=(const Tree &) = delete;

	fs::path root;
};


void WriteText(const fs::path &path, const std::string &text)
{
	std::ofstream{path} << text;
}


// Whether the watcher reports the path within a bound far past any wait the kernel takes.
bool Reports(DirectoryWatcher &watcher, const fs::path &path)
{
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (std::chrono::steady_clock::now() < deadline)
	{
		const std::vector<fs::path> changed = watcher.Wait(50ms);
		if (std::ranges::find(changed, path) != changed.end())
			return true;
	}
	return false;
}
} // namespace


TEST_CASE("DirectoryWatcher: a file written, renamed and deleted is reported, and nothing else")
{
	const Tree tree{"files"};
	DirectoryWatcher watcher{tree.root};
	CHECK(watcher.Wait(0ms).empty());

	WriteText(tree.root / "a.txt", "one");
	CHECK(Reports(watcher, tree.root / "a.txt"));

	// An editor's save: written beside the file, then renamed over it.
	WriteText(tree.root / "a.txt.tmp", "two");
	fs::rename(tree.root / "a.txt.tmp", tree.root / "a.txt");
	CHECK(Reports(watcher, tree.root / "a.txt"));

	fs::rename(tree.root / "a.txt", tree.root / "b.txt");
	CHECK(Reports(watcher, tree.root / "b.txt"));

	fs::remove(tree.root / "b.txt");
	CHECK(Reports(watcher, tree.root / "b.txt"));
	CHECK(watcher.Wait(50ms).empty());
}


TEST_CASE("DirectoryWatcher: a directory made or moved in is watched, one moved out is not")
{
	const Tree tree{"directories"};
	fs::create_directories(tree.root / "old" / "deep");
	DirectoryWatcher watcher{tree.root / "old"};
	WriteText(tree.root / "old" / "deep" / "a.txt", "one");
	CHECK(Reports(watcher, tree.root / "old" / "deep" / "a.txt"));

	fs::create_directories(tree.root / "old" / "new" / "below");
	CHECK(Reports(watcher, tree.root / "old" / "new" / "below"));
	WriteText(tree.root / "old" / "new" / "below" / "b.txt", "two");
	CHECK(Reports(watcher, tree.root / "old" / "new" / "below" / "b.txt"));

	// Made outside, with a file in it, and moved in whole: the file is reported as it is watched.
	fs::create_directories(tree.root / "outside");
	WriteText(tree.root / "outside" / "c.txt", "three");
	fs::rename(tree.root / "outside", tree.root / "old" / "moved");
	CHECK(Reports(watcher, tree.root / "old" / "moved" / "c.txt"));

	fs::rename(tree.root / "old" / "deep", tree.root / "gone");
	CHECK(Reports(watcher, tree.root / "old" / "deep"));
	WriteText(tree.root / "gone" / "d.txt", "four");
	CHECK(watcher.Wait(50ms).empty());
}


TEST_CASE("DirectoryWatcher: a link below the root is not followed, so nothing outside is watched")
{
	const Tree tree{"links"};
	fs::create_directories(tree.root / "watched");
	fs::create_directories(tree.root / "outside");
	fs::create_directory_symlink(tree.root / "outside", tree.root / "watched" / "linked");
	fs::create_directory_symlink(tree.root / "watched", tree.root / "root-link");
	DirectoryWatcher watcher{tree.root / "root-link"};
	WriteText(tree.root / "outside" / "a.txt", "one");
	CHECK(watcher.Wait(200ms).empty());

	// The root is followed: a link to the tree watches the tree.
	WriteText(tree.root / "watched" / "b.txt", "two");
	CHECK(Reports(watcher, tree.root / "root-link" / "b.txt"));
}


TEST_CASE("DirectoryWatcher: what is not a directory is refused, naming it")
{
	const Tree tree{"refused"};
	WriteText(tree.root / "a.txt", "one");
	CHECK_THROWS_WITH_AS(
	    DirectoryWatcher{tree.root / "a.txt"}, doctest::Contains("a.txt"), std::runtime_error);
}
