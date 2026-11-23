#include <jettison/core/FileLock.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <stdexcept>
#include <utility>

namespace Jettison::Core
{
std::optional<FileLock> FileLock::TryAcquire(const std::filesystem::path &path)
{
	// Close-on-exec, or a program this one starts would hold the lock after it.
	FileLock lock{::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600)};
	if (lock.m_descriptor < 0)
		throw std::runtime_error{
		    std::format("{} could not be opened to lock: {}", path.string(), std::strerror(errno))};
	if (::flock(lock.m_descriptor, LOCK_EX | LOCK_NB) == 0)
		return lock;
	if (errno == EWOULDBLOCK)
		return std::nullopt;
	throw std::runtime_error{
	    std::format("{} could not be locked: {}", path.string(), std::strerror(errno))};
}


FileLock::FileLock(int descriptor) : m_descriptor{descriptor}
{
}


FileLock::~FileLock()
{
	if (m_descriptor >= 0)
		::close(m_descriptor);
}


FileLock::FileLock(FileLock &&other) noexcept : m_descriptor{std::exchange(other.m_descriptor, -1)}
{
}


FileLock &FileLock::operator=(FileLock &&other) noexcept
{
	std::swap(m_descriptor, other.m_descriptor);
	return *this;
}
} // namespace Jettison::Core
