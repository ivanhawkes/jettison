#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace Jettison::Core
{
// A file held locked, so that one process at a time works in the directory it stands for, as a
// world's one writer does. The lock is the kernel's (flock) and goes with the process however it
// ends, so a crash leaves nothing stale to clear. It is advisory: it refuses only another
// TryAcquire, the same process's included. For a local file system: over NFS
// Linux stands flock on a lock that does not refuse its own process.
// The most of a lock file's holder that HolderOf reads: a program name is shorter than this.
inline constexpr std::size_t kHolderLimit = 256;

class FileLock
{
  public:
	// Nullopt while another holds it. Makes the file when it is missing, in a directory that is
	// there. Once it holds the lock it writes who does into the file, "<process ID> <program name>"
	// and a newline, over what a crashed holder left. Throws std::runtime_error naming the path when
	// it cannot be opened, locked or written.
	[[nodiscard]] static std::optional<FileLock> TryAcquire(const std::filesystem::path &path);

	// What the lock file says its holder is, for a message such as "the world is already open, held
	// by 1234 manifest-server": its first line, at most kHolderLimit bytes, as it stands. Only the
	// kernel's lock says who holds it: this is read for the message and never trusted, so after a crash
	// it is the dead holder's, and a file that does not read gives an empty string.
	[[nodiscard]] static std::string HolderOf(const std::filesystem::path &path);

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
