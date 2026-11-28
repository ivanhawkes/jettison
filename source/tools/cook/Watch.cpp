#include "Watch.h"

#include <algorithm>
#include <chrono>
#include <exception>
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

// How long a wait for a change lasts before stop is looked at again.
constexpr std::chrono::milliseconds kStopCheck{100};


bool IsSourceChange(const fs::path &path, const fs::path &source)
{
	const fs::path relative = path.lexically_relative(source);
	return std::ranges::none_of(
	    relative, [](const fs::path &part) { return part.string().starts_with('.'); });
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
	while (!stop)
	{
		if (std::ranges::none_of(
		        watcher.Wait(kStopCheck),
		        [&](const fs::path &path) { return IsSourceChange(path, source); }))
			continue;

		// Any change at all, a hidden one too, keeps the tree unsettled.
		while (!stop && !watcher.Wait(kSettle).empty()) {}
		if (!stop)
			Cook(source, output);
	}
}
} // namespace Jettison::Cook
