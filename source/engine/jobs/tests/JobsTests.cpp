#include <jettison/jobs/IoWorker.h>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Jettison::Jobs::IoWorker;

TEST_CASE("Jobs: an empty worker drains and waits for nothing")
{
	IoWorker worker;
	worker.Drain();
	worker.WaitIdle();
}

TEST_CASE("Jobs: work runs on the worker, completions on the thread that drains, both in order")
{
	IoWorker worker;
	const std::thread::id main = std::this_thread::get_id();
	std::vector<int> ran;      // Written by the worker only.
	std::vector<int> finished; // Written by Drain's thread only.
	std::atomic<bool> offMain{true};
	for (int index = 0; index < 100; ++index)
	{
		worker.Enqueue(
		    [&, index]
		    {
			    if (std::this_thread::get_id() == main)
				    offMain = false;
			    ran.push_back(index);
		    },
		    [&, index]
		    {
			    CHECK(std::this_thread::get_id() == main);
			    finished.push_back(index);
		    });
	}
	worker.WaitIdle();
	// WaitIdle has synchronised with the worker, so its writes are visible here.
	CHECK(offMain);
	REQUIRE(ran.size() == 100);
	CHECK(finished.empty());
	worker.Drain();
	REQUIRE(finished.size() == 100);
	for (int index = 0; index < 100; ++index)
	{
		CHECK(ran[static_cast<std::size_t>(index)] == index);
		CHECK(finished[static_cast<std::size_t>(index)] == index);
	}
}

TEST_CASE("Jobs: work that throws skips its completion, and Drain rethrows it on the main thread")
{
	IoWorker worker;
	std::vector<std::string> finished;
	worker.Enqueue([] {}, [&] { finished.emplace_back("before"); });
	worker.Enqueue(
	    [] { throw std::runtime_error{"disk on fire"}; }, [&] { finished.emplace_back("failed"); });
	worker.Enqueue([] {}, [&] { finished.emplace_back("after"); });
	worker.WaitIdle();

	CHECK_THROWS_WITH_AS(worker.Drain(), "disk on fire", std::runtime_error);
	CHECK(finished == std::vector<std::string>{"before"});
	// The worker survived, and the next drain carries on where the last stopped.
	worker.Drain();
	CHECK(finished == std::vector<std::string>{"before", "after"});
	worker.Enqueue([] {}, [&] { finished.emplace_back("later"); });
	worker.WaitIdle();
	worker.Drain();
	CHECK(finished.back() == "later");
}

TEST_CASE("Jobs: a completion can enqueue more work")
{
	IoWorker worker;
	int stage = 0;
	worker.Enqueue(
	    [] {},
	    [&]
	    {
		    stage = 1;
		    worker.Enqueue([] {}, [&] { stage = 2; });
	    });
	worker.WaitIdle();
	worker.Drain();
	CHECK(stage == 1);
	worker.WaitIdle();
	worker.Drain();
	CHECK(stage == 2);
}

TEST_CASE("Jobs: destruction finishes the item in progress and drops the rest")
{
	std::atomic<int> started{0};
	std::promise<void> release;
	const std::shared_future<void> released = release.get_future().share();
	std::thread releaser;
	{
		IoWorker worker;
		worker.Enqueue(
		    [&]
		    {
			    ++started;
			    released.wait();
		    },
		    [] { FAIL("no completion runs after destruction"); });
		for (int index = 0; index < 10; ++index)
			worker.Enqueue([&] { ++started; }, [] { FAIL("no completion runs after destruction"); });
		while (started.load() == 0)
			std::this_thread::yield();
		// The first item is running. It is let go 100 ms from now, by which time the destructor
		// below has requested stop: the margin is for a loaded machine under the sanitisers.
		releaser = std::thread{[&]
		                       {
			                       std::this_thread::sleep_for(std::chrono::milliseconds{100});
			                       release.set_value();
		                       }};
	}
	releaser.join();
	// The destructor joined after the first item, before any of the other ten began.
	CHECK(started.load() == 1);
}
