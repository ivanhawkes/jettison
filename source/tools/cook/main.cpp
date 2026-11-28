#include "CookTree.h"
#include "Watch.h"

#include <signal.h>

#include <atomic>
#include <exception>
#include <filesystem>
#include <string_view>

#include <jettison/core/Log.h>

// jettison_cook <source> <output> [--watch]: the source tree cooked into the output tree once
// (CookTree.h), or watched and cooked as it changes until SIGINT or SIGTERM (Watch.h). Once, it exits 1
// when any source was refused, after the rest are cooked and each refusal is reported.

namespace
{
constexpr Jettison::Core::Category kCook{"cook"};

constexpr int kUsageError = 2;

constexpr std::string_view kWatchFlag = "--watch";

// A signal handler may touch only a lock-free atomic.
std::atomic<bool> g_stop{false};
static_assert(std::atomic<bool>::is_always_lock_free);


void StopOnSignals()
{
	struct sigaction action{};
	action.sa_handler = [](int) { g_stop = true; };
	sigemptyset(&action.sa_mask);
	for (const int signal : {SIGINT, SIGTERM})
		sigaction(signal, &action, nullptr);
}
} // namespace


int main(int argc, char **argv)
{
	using Jettison::Core::Log;
	using Jettison::Core::Severity;

	const bool watch = argc == 4 && argv[3] == kWatchFlag;
	if (argc != 3 && !watch)
	{
		Log(Severity::Error, kCook, "Usage: jettison_cook <source> <output> [--watch]");
		return kUsageError;
	}
	const std::filesystem::path source{argv[1]};
	const std::filesystem::path output{argv[2]};
	try
	{
		if (!watch)
			return Jettison::Cook::CookTree(source, output).refusals.empty() ? 0 : 1;
		StopOnSignals();
		Jettison::Cook::WatchTree(source, output, g_stop);
		return 0;
	}
	catch (const std::exception &error)
	{
		Log(Severity::Error, kCook, "{}", error.what());
		return 1;
	}
}
