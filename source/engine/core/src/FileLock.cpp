#include <jettison/core/FileLock.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
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
	if (::flock(lock.m_descriptor, LOCK_EX | LOCK_NB) != 0)
	{
		if (errno == EWOULDBLOCK)
			return std::nullopt;
		throw std::runtime_error{
		    std::format("{} could not be locked: {}", path.string(), std::strerror(errno))};
	}

	// Written over before it is cut to length, so that a reader never finds the file empty.
	const std::string holder = std::format("{} {}\n", ::getpid(), program_invocation_short_name);
	const ::ssize_t written = ::pwrite(lock.m_descriptor, holder.data(), holder.size(), 0);
	if (written != static_cast<::ssize_t>(holder.size()) ||
	    ::ftruncate(lock.m_descriptor, static_cast<::off_t>(holder.size())) != 0)
		throw std::runtime_error{std::format(
		    "{} could not be written with its holder: {}", path.string(), std::strerror(errno))};
	return lock;
}


std::string FileLock::HolderOf(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary};
	std::string holder(kHolderLimit, '\0');
	file.read(holder.data(), static_cast<std::streamsize>(holder.size()));
	holder.resize(static_cast<std::size_t>(file.gcount()));
	return holder.substr(0, holder.find('\n'));
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
