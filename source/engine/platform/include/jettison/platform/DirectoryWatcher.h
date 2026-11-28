#pragma once

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace Jettison::Platform
{
// Watches a directory and every directory below it for what changes there: a file written and closed,
// a file or directory made or deleted, or renamed into, out of or within the tree. A directory made or
// moved in is watched from then on, and what is already in it is reported, since they can be
// written before its watch is added; one moved out is no longer watched. A symbolic link below the
// root is reported as a file and not followed, so nothing outside the tree is watched. On Linux it is
// inotify, whose queue is per user and bounded: a change lost to its overflow is reported as the root,
// and the tree's directories are watched again. One thread at a time.
class DirectoryWatcher
{
  public:
	// Throws std::runtime_error naming the directory when it is not one or cannot be watched.
	explicit DirectoryWatcher(const std::filesystem::path &root);

	~DirectoryWatcher();

	DirectoryWatcher(const DirectoryWatcher &) = delete;

	DirectoryWatcher &operator=(const DirectoryWatcher &) = delete;

	// Waits up to the timeout for a change, then returns the paths of every change since the last call,
	// each the root joined to its relative path, in the order seen, a path as often as it changed.
	// Empty when nothing changed in time or a signal cut the wait short. Throws std::runtime_error when
	// the system's wait or read fails.
	std::vector<std::filesystem::path> Wait(std::chrono::milliseconds timeout);

  private:
	struct State;

	std::unique_ptr<State> m_pState;
};
} // namespace Jettison::Platform
