#pragma once

#include <filesystem>
#include <optional>

namespace Jettison::Core
{
// A file held locked, so that one process at a time works in the directory it stands for, as a
// world's one writer does. The lock is the kernel's (flock) and goes with the process however it
// ends, so a crash leaves nothing stale to clear; the file stays, and holds nothing. It is advisory:
// it refuses only another TryAcquire, the same process's included. For a local file system: over NFS
// Linux stands flock on a lock that does not refuse its own process.
class FileLock
{
  public:
	// Nullopt while another holds it. Makes the file when it is missing, in a directory that is
	// there. Throws std::runtime_error naming the path when it cannot be opened or locked.
	[[nodiscard]] static std::optional<FileLock> TryAcquire(const std::filesystem::path &path);

	~FileLock();

	FileLock(FileLock &&other) noexcept;

	FileLock &operator=(FileLock &&other) noexcept;

	FileLock(const FileLock &) = delete;

	FileLock &operator=(const FileLock &) = delete;

  private:
	explicit FileLock(int descriptor);

	int m_descriptor;
};
} // namespace Jettison::Core
