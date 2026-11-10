#include <jettison/resource/ResourceHandle.h>
#include <jettison/resource/ResourceManager.h>

#include <jettison/jobs/IoWorker.h>
#include <jettison/vfs/FileSystem.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Jettison;
using Resource::Status;

namespace
{
struct Text
{
	std::string contents;
};

struct Length
{
	std::size_t bytes{0};
};

// A base and a mod mount of their own per test case, removed when the fixture goes.
class Scratch
{
  public:
	Scratch()
	    : m_directory{
	          std::filesystem::temp_directory_path() /
	          ("jettison-resource-tests-" + std::to_string(getpid()))}
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
	void Remove(const std::string &path) const
	{
		std::filesystem::remove(m_directory / path);
	}
	const Vfs::FileSystem &Files() const
	{
		return m_fileSystem;
	}

  private:
	std::filesystem::path m_directory;
	Vfs::FileSystem m_fileSystem;
};

// A Text is the file's bytes; a file that says "bad" is refused, as a parser refuses bad data.
Resource::Loader<Text> TextLoader()
{
	return {
	    [](std::span<const std::byte> bytes, std::string_view)
	    {
		    std::string contents{reinterpret_cast<const char *>(bytes.data()), bytes.size()};
		    if (contents == "bad")
			    throw std::runtime_error{"the text is bad"};
		    return std::make_unique<Text>(Text{std::move(contents)});
	    },
	    {},
	    {}};
}

// Finishes the loads the worker has, as a frame's Update does once they are done.
void Settle(Jobs::IoWorker &worker, Resource::ResourceManager &manager)
{
	worker.WaitIdle();
	manager.Update();
}

Resource::ResourceOptions NoHotReload()
{
	return {.hotReload = false, .pollInterval = {}};
}
} // namespace

TEST_CASE("Resource: a default handle is null and handles compare by index and generation")
{
	using Handle = Resource::ResourceHandle<Text>;
	CHECK(Handle{}.IsNull());
	CHECK(Handle{3, 1} == Handle{3, 1});
	CHECK(Handle{3, 1} != Handle{3, 2});
}

TEST_CASE("Resource: a load returns at once, and the value is there after the next Update")
{
	const Scratch scratch;
	scratch.Put("base/greeting.txt", "hello");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	manager.RegisterLoader(TextLoader());

	const auto handle = manager.Load<Text>("greeting.txt");
	CHECK_FALSE(handle.IsNull());
	CHECK(manager.StatusOf(handle) == Status::Loading);
	CHECK(manager.TryGet(handle) == nullptr);
	CHECK(manager.RevisionOf(handle) == 0);

	Settle(worker, manager);
	CHECK(manager.StatusOf(handle) == Status::Ready);
	REQUIRE(manager.TryGet(handle) != nullptr);
	CHECK(manager.TryGet(handle)->contents == "hello");
	CHECK(manager.RevisionOf(handle) == 1);
	CHECK(manager.ErrorOf(handle).empty());
}

TEST_CASE("Resource: loads of one path share a slot, counted, and the last release frees it")
{
	const Scratch scratch;
	scratch.Put("base/greeting.txt", "hello");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	manager.RegisterLoader(TextLoader());

	const auto first = manager.Load<Text>("greeting.txt");
	const auto second = manager.Load<Text>("greeting.txt");
	CHECK(first == second);
	Settle(worker, manager);

	manager.Release(first);
	CHECK(manager.TryGet(second) != nullptr);
	manager.Release(second);
	CHECK(manager.StatusOf(second) == Status::Invalid);
	CHECK(manager.TryGet(second) == nullptr);
	CHECK_THROWS_WITH_AS(
	    manager.Release(second), doctest::Contains("released a stale handle"), std::logic_error);

	// The slot is reused under a new generation, so the old handle stays stale.
	scratch.Put("base/other.txt", "other");
	const auto reused = manager.Load<Text>("other.txt");
	CHECK(reused.index == first.index);
	CHECK(reused.generation != first.generation);
	Settle(worker, manager);
	CHECK(manager.TryGet(first) == nullptr);
	CHECK(manager.TryGet(reused)->contents == "other");
}

TEST_CASE("Resource: each type has its own table, so one path can be two resources")
{
	const Scratch scratch;
	scratch.Put("base/greeting.txt", "hello");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	manager.RegisterLoader(TextLoader());
	manager.RegisterLoader(
	    Resource::Loader<Length>{
	        [](std::span<const std::byte> bytes, std::string_view)
	        { return std::make_unique<Length>(Length{bytes.size()}); },
	        {},
	        {}});

	const auto text = manager.Load<Text>("greeting.txt");
	const auto length = manager.Load<Length>("greeting.txt");
	Settle(worker, manager);
	CHECK(manager.TryGet(text)->contents == "hello");
	CHECK(manager.TryGet(length)->bytes == 5);
}

TEST_CASE("Resource: a missing file or bad data fails the resource, with the reason kept")
{
	const Scratch scratch;
	scratch.Put("base/bad.txt", "bad");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	manager.RegisterLoader(TextLoader());

	const auto missing = manager.Load<Text>("missing.txt");
	const auto bad = manager.Load<Text>("bad.txt");
	Settle(worker, manager);
	CHECK(manager.StatusOf(missing) == Status::Failed);
	CHECK(manager.ErrorOf(missing) == "'missing.txt' is not in any mount");
	CHECK(manager.TryGet(missing) == nullptr);
	CHECK(manager.StatusOf(bad) == Status::Failed);
	CHECK(manager.ErrorOf(bad) == "the text is bad");
	// A failed resource still holds its reference until it is released.
	manager.Release(bad);
	CHECK(manager.StatusOf(bad) == Status::Invalid);
}

TEST_CASE("Resource: misuse is refused at the call")
{
	const Scratch scratch;
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	CHECK_THROWS_WITH_AS(
	    (void)manager.Load<Text>("greeting.txt"), doctest::Contains("no loader is registered"),
	    std::logic_error);
	manager.RegisterLoader(TextLoader());
	CHECK_THROWS_WITH_AS(
	    manager.RegisterLoader(TextLoader()), doctest::Contains("already registered"),
	    std::logic_error);
	CHECK_THROWS_AS(manager.RegisterLoader(Resource::Loader<Length>{}), std::logic_error);
	CHECK_THROWS_AS((void)manager.Load<Text>("../escape.txt"), std::invalid_argument);
	CHECK_THROWS_AS(manager.Release(Resource::ResourceHandle<Text>{}), std::logic_error);
}

TEST_CASE("Resource: decode runs on the worker and finish on the main thread, and finish can fail")
{
	const Scratch scratch;
	scratch.Put("base/greeting.txt", "hello");
	scratch.Put("base/refused.txt", "refused");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	const std::thread::id main = std::this_thread::get_id();
	bool decodedOffMain = true;
	bool finishedOnMain = true;
	manager.RegisterLoader(
	    Resource::Loader<Text>{
	        [&](std::span<const std::byte> bytes, std::string_view)
	        {
		        decodedOffMain = decodedOffMain && std::this_thread::get_id() != main;
		        return std::make_unique<Text>(
		            Text{std::string{reinterpret_cast<const char *>(bytes.data()), bytes.size()}});
	        },
	        [&](Text &text, std::string_view path)
	        {
		        finishedOnMain = finishedOnMain && std::this_thread::get_id() == main;
		        if (path == "refused.txt")
			        throw std::runtime_error{"the upload was refused"};
		        text.contents += ", finished";
	        },
	        {}});

	const auto greeting = manager.Load<Text>("greeting.txt");
	const auto refused = manager.Load<Text>("refused.txt");
	Settle(worker, manager);
	CHECK(decodedOffMain);
	CHECK(finishedOnMain);
	CHECK(manager.TryGet(greeting)->contents == "hello, finished");
	CHECK(manager.StatusOf(refused) == Status::Failed);
	CHECK(manager.ErrorOf(refused) == "the upload was refused");
}

TEST_CASE("Resource: a release while the load is in flight drops its result")
{
	const Scratch scratch;
	scratch.Put("base/greeting.txt", "hello");
	scratch.Put("base/other.txt", "other");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	manager.RegisterLoader(TextLoader());

	// Hold the worker so the load stays in flight until the slot has been released and reused.
	std::promise<void> open;
	worker.Enqueue([gate = open.get_future().share()] { gate.wait(); }, {});
	const auto released = manager.Load<Text>("greeting.txt");
	manager.Release(released);
	const auto reused = manager.Load<Text>("other.txt");
	REQUIRE(reused.index == released.index);
	open.set_value();
	Settle(worker, manager);

	CHECK(manager.TryGet(released) == nullptr);
	REQUIRE(manager.TryGet(reused) != nullptr);
	CHECK(manager.TryGet(reused)->contents == "other");
}

TEST_CASE("Resource: a manager destroyed with work in flight drops the completions")
{
	const Scratch scratch;
	scratch.Put("base/greeting.txt", "hello");
	Jobs::IoWorker worker;
	{
		Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
		manager.RegisterLoader(TextLoader());
		(void)manager.Load<Text>("greeting.txt");
	}
	worker.WaitIdle();
	worker.Drain();
}

TEST_CASE(
    "Resource: hot reload keeps the handle, bumps the revision, and keeps the old value on failure")
{
	const Scratch scratch;
	scratch.Put("base/greeting.txt", "hello");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	manager.RegisterLoader(TextLoader());
	const auto handle = manager.Load<Text>("greeting.txt");
	Settle(worker, manager);

	// Nothing changed, so nothing reloads.
	manager.CheckForChanges();
	Settle(worker, manager);
	CHECK(manager.RevisionOf(handle) == 1);

	// The sizes differ at each step, so the change is seen whatever the clock's resolution.
	scratch.Put("base/greeting.txt", "hello again");
	manager.CheckForChanges();
	Settle(worker, manager);
	CHECK(manager.TryGet(handle)->contents == "hello again");
	CHECK(manager.RevisionOf(handle) == 2);

	// A mod shadowing the file is a change too.
	scratch.Put("mod/greeting.txt", "modded hello");
	manager.CheckForChanges();
	Settle(worker, manager);
	CHECK(manager.TryGet(handle)->contents == "modded hello");
	CHECK(manager.RevisionOf(handle) == 3);

	// A bad save keeps the last good value, with the reason.
	scratch.Put("mod/greeting.txt", "bad");
	manager.CheckForChanges();
	Settle(worker, manager);
	CHECK(manager.StatusOf(handle) == Status::Ready);
	CHECK(manager.TryGet(handle)->contents == "modded hello");
	CHECK(manager.RevisionOf(handle) == 3);
	CHECK(manager.ErrorOf(handle) == "the text is bad");

	// Removing the mod's file falls back to the base, and the error clears.
	scratch.Remove("mod/greeting.txt");
	manager.CheckForChanges();
	Settle(worker, manager);
	CHECK(manager.TryGet(handle)->contents == "hello again");
	CHECK(manager.RevisionOf(handle) == 4);
	CHECK(manager.ErrorOf(handle).empty());
}

TEST_CASE("Resource: hot reload retries a failed resource once its file appears, from Update")
{
	const Scratch scratch;
	Jobs::IoWorker worker;
	// A zero interval polls on every Update.
	Resource::ResourceManager manager{scratch.Files(), worker, {.hotReload = true, .pollInterval = {}}};
	manager.RegisterLoader(TextLoader());
	const auto handle = manager.Load<Text>("late.txt");
	Settle(worker, manager);
	CHECK(manager.StatusOf(handle) == Status::Failed);

	scratch.Put("base/late.txt", "here now");
	manager.Update(); // Sees the change and starts the reload.
	Settle(worker, manager);
	CHECK(manager.StatusOf(handle) == Status::Ready);
	CHECK(manager.TryGet(handle)->contents == "here now");
}

TEST_CASE("Resource: retire sees a value a reload replaced and a value the last release dropped")
{
	const Scratch scratch;
	scratch.Put("base/note.txt", "first");
	Jobs::IoWorker worker;
	Resource::ResourceManager manager{scratch.Files(), worker, NoHotReload()};
	auto retired = std::make_shared<std::vector<std::string>>();
	Resource::Loader<Text> loader = TextLoader();
	loader.retire = [retired](Text &text) { retired->push_back(text.contents); };
	manager.RegisterLoader(std::move(loader));

	const auto handle = manager.Load<Text>("note.txt");
	Settle(worker, manager);
	CHECK(retired->empty());

	// A failed reload keeps the value, so nothing is retired.
	scratch.Put("base/note.txt", "bad");
	manager.ReloadAll();
	Settle(worker, manager);
	CHECK(retired->empty());
	CHECK(manager.TryGet(handle)->contents == "first");

	// ReloadAll reads the file whether or not its stamp moved.
	scratch.Put("base/note.txt", "second");
	manager.ReloadAll();
	Settle(worker, manager);
	CHECK(*retired == std::vector<std::string>{"first"});
	CHECK(manager.TryGet(handle)->contents == "second");
	CHECK(manager.RevisionOf(handle) == 2);

	manager.Release(handle);
	CHECK(*retired == std::vector<std::string>{"first", "second"});
}
