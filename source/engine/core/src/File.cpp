#include <jettison/core/File.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <format>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace Jettison::Core
{
namespace
{
class Descriptor
{
  public:
	explicit Descriptor(int descriptor) : m_descriptor{descriptor}
	{
	}


	~Descriptor()
	{
		if (m_descriptor >= 0)
			::close(m_descriptor);
	}


	Descriptor(const Descriptor &) = delete;


	Descriptor &operator=(const Descriptor &) = delete;


	int Get() const
	{
		return m_descriptor;
	}


	// Closes it now, and says whether the close reported an error: on some file systems a write's
	// failure is reported only there.
	bool Close()
	{
		const int descriptor = m_descriptor;
		m_descriptor = -1;
		return ::close(descriptor) == 0;
	}

  private:
	int m_descriptor;
};


std::string Problem(std::string_view what)
{
	return std::format("{}: {}", what, std::strerror(errno));
}


enum class Replace : std::uint8_t
{
	Allowed,
	Refused,
};


// A name to write a file under that no other writer of it uses: <name>:partial.<process ID>.<count>.
std::filesystem::path UniquePartial(const std::filesystem::path &path)
{
	static std::atomic<unsigned> counter;
	std::filesystem::path partial = path;
	partial += std::format("{}.{}.{}", kPartialSuffix, ::getpid(), counter++);
	return partial;
}


// Whether a partial file in the directory is one a writer of the path left that is gone: the
// <name>:partial of a write before partials were named for their writers, or one named for a process
// that no longer runs. One of a process that runs, this one's other writers' included, is in use.
bool Abandoned(const std::string &entry, const std::string &prefix)
{
	if (!entry.starts_with(prefix))
		return false;
	const std::string_view rest = std::string_view{entry}.substr(prefix.size());
	if (rest.empty())
		return true;
	::pid_t writer = 0;
	const auto [end, error] = std::from_chars(rest.data() + 1, rest.data() + rest.size(), writer);
	if (rest.front() != '.' || error != std::errc{} || end == rest.data() + rest.size() || *end != '.')
		return false;
	return ::kill(writer, 0) != 0 && errno == ESRCH;
}


// The partial files writers of the path left as they crashed, removed so they do not gather.
void RemoveAbandoned(const std::filesystem::path &path, const std::filesystem::path &directory)
{
	const std::string prefix = path.filename().string() + std::string{kPartialSuffix};
	std::error_code error;
	for (std::filesystem::directory_iterator entry{directory, error};
	     !error && entry != std::filesystem::directory_iterator{}; entry.increment(error))
		if (std::error_code ignored; Abandoned(entry->path().filename().string(), prefix))
			std::filesystem::remove(entry->path(), ignored);
}


// Zero, or the errno of what failed. The one Linux-specific call here: Windows has no rename that
// refuses to replace, short of MoveFileEx without its replace flag.
int RenameWithoutReplacing(const std::filesystem::path &from, const std::filesystem::path &to)
{
	return ::renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE) == 0 ? 0 : errno;
}


// Zero, or the errno of what failed.
int FlushDirectory(const std::filesystem::path &directory)
{
	const Descriptor descriptor{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
	return descriptor.Get() >= 0 && ::fsync(descriptor.Get()) == 0 ? 0 : errno;
}
} // namespace


bool CreateDirectoriesDurably(const std::filesystem::path &path, std::filesystem::perms permissions)
{
	std::error_code error;
	std::vector<std::filesystem::path> missing;
	for (std::filesystem::path directory = path;
	     !directory.empty() && !std::filesystem::exists(directory, error);
	     directory = directory.parent_path())
		missing.push_back(directory);

	// Outermost first: a directory flushed into a parent that is not on the disk is lost with it.
	for (const std::filesystem::path &directory : std::views::reverse(missing))
	{
		// One already there is a path through a directory just made and back out, as new/..
		if (::mkdir(directory.c_str(), static_cast<mode_t>(permissions)) != 0)
		{
			const int made = errno;
			if (made != EEXIST || !std::filesystem::is_directory(directory, error))
				throw std::runtime_error{
				    std::format("{} could not be made: {}", directory.string(), std::strerror(made))};
		}
		const int problem = FlushDirectory(directory.has_parent_path() ? directory.parent_path() : ".");
		if (problem != 0)
			throw std::runtime_error{std::format(
			    "{} was made, but its directory could not be flushed: {}", directory.string(),
			    std::strerror(problem))};
	}
	if (!std::filesystem::is_directory(path, error))
		throw std::runtime_error{std::format("{} is not a directory", path.string())};
	return !missing.empty();
}


namespace
{
void WriteDurably(
    const std::filesystem::path &path, std::span<const std::byte> bytes,
    std::filesystem::perms permissions, Replace replace)
{
	const std::filesystem::path partial = UniquePartial(path);
	const auto fail = [&](const std::string &problem)
	{
		std::error_code ignored;
		std::filesystem::remove(partial, ignored);
		return std::runtime_error{std::format("{} could not be written: {}", path.string(), problem)};
	};

	const std::filesystem::path directory =
	    path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
	CreateDirectoriesDurably(directory);

	{
		// One a crash left is removed, not reused, since an open of a file there keeps its mode.
		RemoveAbandoned(path, directory);
		Descriptor file{::open(
		    partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
		    static_cast<mode_t>(permissions))};
		if (file.Get() < 0)
			throw fail(Problem("open"));
		for (std::size_t written = 0; written < bytes.size();)
		{
			const ::ssize_t count = ::write(file.Get(), bytes.data() + written, bytes.size() - written);
			if (count < 0 && errno == EINTR)
				continue;
			if (count < 0)
				throw fail(Problem("write"));
			written += static_cast<std::size_t>(count);
		}
		if (::fsync(file.Get()) != 0)
			throw fail(Problem("fsync"));
		if (!file.Close())
			throw fail(Problem("close"));
	}

	if (replace == Replace::Allowed)
	{
		// rename replaces the old file in one step on POSIX.
		std::error_code error;
		std::filesystem::rename(partial, path, error);
		if (error)
			throw fail(error.message());
	}
	else if (const int problem = RenameWithoutReplacing(partial, path); problem == EEXIST)
	{
		const std::runtime_error refused = fail("a file is already there");
		throw FileExistsError{refused.what()};
	}
	else if (problem != 0)
		throw fail(std::strerror(problem));

	// The file is complete under its name; what remains is the directory's entry for it. A failure
	// here leaves the new file in place, which is reported, since it may not survive a power cut.
	const int problem = FlushDirectory(directory);
	if (problem != 0)
		throw std::runtime_error{std::format(
		    "{} was written, but its directory could not be flushed: {}", path.string(),
		    std::strerror(problem))};
}


} // namespace


void WriteFileDurably(
    const std::filesystem::path &path, std::span<const std::byte> bytes,
    std::filesystem::perms permissions)
{
	WriteDurably(path, bytes, permissions, Replace::Allowed);
}


void WriteFileDurably(
    const std::filesystem::path &path, std::string_view text, std::filesystem::perms permissions)
{
	WriteFileDurably(path, std::as_bytes(std::span{text.data(), text.size()}), permissions);
}


void WriteNewFileDurably(
    const std::filesystem::path &path, std::span<const std::byte> bytes,
    std::filesystem::perms permissions)
{
	WriteDurably(path, bytes, permissions, Replace::Refused);
}


void WriteNewFileDurably(
    const std::filesystem::path &path, std::string_view text, std::filesystem::perms permissions)
{
	WriteNewFileDurably(path, std::as_bytes(std::span{text.data(), text.size()}), permissions);
}
} // namespace Jettison::Core
