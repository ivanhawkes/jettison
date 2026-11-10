#include <jettison/jobs/IoWorker.h>

#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace Jettison::Jobs
{
namespace
{
struct Item
{
	IoWorker::Work work;
	IoWorker::Completion completion;
};

struct Finished
{
	IoWorker::Completion completion;
	std::exception_ptr error;
};
} // namespace

struct IoWorker::State
{
	std::mutex mutex;
	// Woken by Enqueue and by the stop request.
	std::condition_variable_any workAvailable;
	// Woken when the worker goes idle.
	std::condition_variable_any idle;
	std::deque<Item> pending;
	std::deque<Finished> finished;
	bool busy{false};
	// Declared last, so it is destroyed, and joined, first: the thread uses everything above.
	std::jthread thread;

	void Run(const std::stop_token &stop)
	{
		std::unique_lock lock{mutex};
		for (;;)
		{
			workAvailable.wait(lock, stop, [this] { return !pending.empty(); });
			// Checked after the wait, not by its result: with work pending the wait returns true
			// even once stop is requested, and the destructor drops what has not started.
			if (stop.stop_requested())
				return;
			Item item = std::move(pending.front());
			pending.pop_front();
			busy = true;
			lock.unlock();

			std::exception_ptr error;
			try
			{
				item.work();
			}
			catch (...)
			{
				error = std::current_exception();
			}
			// The work item's captures are released here, on the worker, so what they hold (a
			// file's bytes, say) is freed off the main thread.
			item.work = nullptr;

			lock.lock();
			finished.push_back({std::move(item.completion), std::move(error)});
			busy = false;
			if (pending.empty())
				idle.notify_all();
		}
	}
};

IoWorker::IoWorker() : m_pState{std::make_unique<State>()}
{
	m_pState->thread =
	    std::jthread{[state = m_pState.get()](const std::stop_token &stop) { state->Run(stop); }};
}

// The jthread requests stop and joins as State is destroyed; the wait above wakes on the request.
IoWorker::~IoWorker() = default;

void IoWorker::Enqueue(Work work, Completion completion)
{
	{
		const std::scoped_lock lock{m_pState->mutex};
		m_pState->pending.push_back({std::move(work), std::move(completion)});
	}
	m_pState->workAvailable.notify_one();
}

void IoWorker::Drain()
{
	for (;;)
	{
		Finished item;
		{
			const std::scoped_lock lock{m_pState->mutex};
			if (m_pState->finished.empty())
				return;
			item = std::move(m_pState->finished.front());
			m_pState->finished.pop_front();
		}
		// Outside the lock, so a completion can enqueue more work.
		if (item.error)
			std::rethrow_exception(item.error);
		if (item.completion)
			item.completion();
	}
}

void IoWorker::WaitIdle()
{
	std::unique_lock lock{m_pState->mutex};
	m_pState->idle.wait(lock, [this] { return m_pState->pending.empty() && !m_pState->busy; });
}
} // namespace Jettison::Jobs
