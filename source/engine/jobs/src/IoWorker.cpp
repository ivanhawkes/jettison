#include <jettison/jobs/IoWorker.h>

#include <jettison/core/Assert.h>

namespace Jettison::Jobs
{
struct IoWorker::State
{
};

IoWorker::IoWorker() = default;
IoWorker::~IoWorker() = default;

void IoWorker::Enqueue(Work /*unused*/, Completion /*unused*/)
{
	JETTISON_NOT_IMPLEMENTED();
}
void IoWorker::Drain()
{
}
void IoWorker::WaitIdle()
{
}
} // namespace Jettison::Jobs
