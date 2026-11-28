#include "Watch.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <vector>

#include "CookTree.h"
#include <jettison/core/Log.h>
#include <jettison/platform/DirectoryWatcher.h>

namespace Jettison::Cook
{
namespace
{
namespace fs = std::filesystem;

constexpr Core::Category kCook{"cook"};

// How long the source must be quiet before a cook: long beside the gaps between the files one save
// writes back to back (a model's .gltf, .bin and images), short beside the cook an artist then waits
// on.
constexpr std::chrono::milliseconds kSettle{250};

// The longest a cook waits after the first change for the source to settle, so changes that never
// stop (a build or a git checkout writing the tree, a tool saving every 100 ms) cannot hold it back:
// long beside one save's burst of files, short beside the cook an artist then waits on. A cook in
// mid-burst refuses a half-written source, keeping its outputs, and the burst's next change cooks it.
constexpr std::chrono::seconds kMostSettle{2};

// How long a wait for a change lasts before stop is looked at again.
constexpr std::chrono::milliseconds kStopCheck{100};


bool HasSourceChange(const std::vector<fs::path> &paths, const fs::path &source)
{
	return std::ranges::any_of(
	    paths, [&](const fs::path &path) { return !IsHidden(path.lexically_relative(source)); });
}


void Cook(const fs::path &source, const fs::path &output)
{
	try
	{
		CookTree(source, output);
	}
	catch (const std::exception &error)
	{
		Core::Log(Core::Severity::Error, kCook, "{}", error.what());
	}
}
} // namespace


void WatchTree(const fs::path &source, const fs::path &output, const std::atomic<bool> &stop)
{
	CheckTrees(source, output);

	// Watching before the first cook, so a change made during it is seen.
	Platform::DirectoryWatcher watcher{source};
	Cook(source, output);
	Core::Log(Core::Severity::Info, kCook, "watching {}", source.string());
	using Clock = std::chrono::steady_clock;
	while (!stop)
	{
		if (!HasSourceChange(watcher.Wait(kStopCheck), source))
			continue;

		// Only a change that could start a cook keeps the tree unsettled, and only until the deadline.
		const Clock::time_point deadline = Clock::now() + kMostSettle;
		Clock::time_point cookAt = Clock::now() + kSettle;
		while (!stop && Clock::now() < cookAt)
		{
			const auto left = std::chrono::ceil<std::chrono::milliseconds>(cookAt - Clock::now());
			if (HasSourceChange(
			        watcher.Wait(std::clamp(left, std::chrono::milliseconds{0}, kStopCheck)), source))
				cookAt = std::min(Clock::now() + kSettle, deadline);
		}
		if (!stop)
			Cook(source, output);
	}
}
} // namespace Jettison::Cook
