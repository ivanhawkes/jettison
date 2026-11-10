#pragma once

#include <functional>
#include <memory>

namespace Jettison::Jobs
{
// One worker thread that reads files and decodes assets for the resource manager, so a system
// entry does not stall the frame loop. Work is a callable; completion is reported back on the
// main thread through Drain(), which the frame loop calls once per frame, so nothing the worker
// finished touches engine state from another thread. The general job system is deferred until a
// profile asks for it (docs/scope.md); when it comes, it is a second type beside this one.
//
// Work runs in the order it was enqueued, one item at a time, and completions run in the same
// order. Work that throws does not take the worker down: its completion is skipped and Drain()
// rethrows the exception on the main thread, so a failure is never silent. Work should still
// catch what it expects to fail, as the resource manager does for a load, and hand the outcome to
// its completion.
class IoWorker
{
  public:
	using Work = std::function<void()>;
	using Completion = std::function<void()>;

	IoWorker();
	// Finishes the item in progress, then stops. Work not yet started is dropped, and no
	// completion runs: call WaitIdle() and Drain() first to finish everything.
	~IoWorker();
	IoWorker(const IoWorker &) = delete;
	IoWorker &operator=(const IoWorker &) = delete;

	// Runs work on the worker, then completion on a later Drain(). Safe from any thread.
	void Enqueue(Work work, Completion completion);
	// Runs the completions of finished work, in order. Main thread only. If a work item threw,
	// the completions before it run, then its exception is rethrown; the ones after it wait for
	// the next Drain().
	void Drain();
	// Blocks until every item enqueued so far has run on the worker. Their completions wait for
	// Drain(). Used by shutdown and by tests.
	void WaitIdle();

  private:
	struct State;
	std::unique_ptr<State> m_pState;
};
} // namespace Jettison::Jobs
