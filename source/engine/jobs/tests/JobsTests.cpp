#include <jettison/jobs/IoWorker.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Jobs: an empty worker drains and waits for nothing; enqueueing fails loudly until phase 3")
{
	Jettison::Jobs::IoWorker worker;
	worker.Drain();
	worker.WaitIdle();
	CHECK_THROWS_AS(worker.Enqueue([] {}, [] {}), Jettison::Core::NotImplementedError);
}
