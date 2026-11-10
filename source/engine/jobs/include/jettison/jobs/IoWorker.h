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
// Filled in phase 3.
class IoWorker
{
  public:
	using Work = std::function<void()>;
	using Completion = std::function<void()>;

	IoWorker();
	~IoWorker();
	IoWorker(const IoWorker &) = delete;
	IoWorker &operator=(const IoWorker &) = delete;

	// Runs work on the worker, then completion on the next Drain().
	void Enqueue(Work work, Completion completion);
	// Runs the completions of finished work. Main thread only.
	void Drain();
	// Blocks until the queue is empty. Used by shutdown and by tests.
	void WaitIdle();

  private:
	struct State;
	std::unique_ptr<State> m_pState;
};
} // namespace Jettison::Jobs
