#include <jettison/core/File.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>

#include <atomic>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <mutex>
#include <random>
#include <ranges>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace Jettison::Core
{
namespace
{
// The bits of a process mark each of its two draws of std::random_device gives, and the base a
// partial's name writes the mark in.
constexpr unsigned kMarkHalfBits = 32;
constexpr int kHexadecimal = 16;


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


// This process's mark in its partial files' names, drawn once: a process given the ID a crashed one
// had cannot draw that one's mark too, so it never meets a partial of its own name.
std::uint64_t ProcessMark()
{
	static const std::uint64_t mark = []
	{
		std::random_device source;
		return (std::uint64_t{source()} << kMarkHalfBits) | source();
	}();
	return mark;
}


// A partial's name: :partial.<process ID>.<process mark>.<count>, the mark every hexadecimal digit.
std::string PartialName(::pid_t writer, std::uint64_t mark, unsigned count)
{
	return std::format("{}.{}.{:016x}.{}", kPartialPrefix, writer, mark, count);
}


// A name in the path's directory to write it under that no other writer uses, bounded in length.
std::filesystem::path UniquePartial(const std::filesystem::path &path)
{
	static std::atomic<unsigned> counter;
	return path.parent_path() / PartialName(::getpid(), ProcessMark(), counter++);
}


// Whether a directory's entry is a partial file a writer that is gone left: a name PartialName gives,
// spelt as it spells it, of a process that no longer runs. One of a process that runs, this one's
// other writers' included, may be in use, and any other name, a near one included, is not a partial.
bool Abandoned(std::string_view entry)
{
	if (!entry.starts_with(kPartialPrefix) || entry.size() == kPartialPrefix.size() ||
	    entry[kPartialPrefix.size()] != '.')
		return false;
	const char *const end = entry.data() + entry.size();
	::pid_t writer = 0;
	const auto [afterWriter, writerError] =
	    std::from_chars(entry.data() + kPartialPrefix.size() + 1, end, writer);
	if (writerError != std::errc{} || writer <= 0 || afterWriter == end || *afterWriter != '.')
		return false;
	std::uint64_t mark = 0;
	const auto [afterMark, markError] = std::from_chars(afterWriter + 1, end, mark, kHexadecimal);
	if (markError != std::errc{} || afterMark == end || *afterMark != '.')
		return false;
	unsigned count = 0;
	const auto [afterCount, countError] = std::from_chars(afterMark + 1, end, count);
	if (countError != std::errc{} || afterCount != end || entry != PartialName(writer, mark, count))
		return false;
	return ::kill(writer, 0) != 0 && errno == ESRCH;
}


// Whether this is the process's first write into the directory. A partial a crash left is an
// earlier process's, so the first write's look at the directory finds every one there is, and a
// later write need not look again.
bool FirstWriteIn(const std::filesystem::path &directory)
{
	static std::mutex mutex;
	static std::unordered_set<std::string> seen;
	std::error_code error;
	const std::filesystem::path absolute = std::filesystem::absolute(directory, error);
	const std::scoped_lock lock{mutex};
	return seen.insert((error ? directory : absolute).lexically_normal().string()).second;
}


// The partial files in the target's directory that writers which are gone left, removed so they do
// not gather: once a process, by FirstWriteIn, and never the target itself, whatever its name.
void RemoveAbandoned(const std::filesystem::path &target, const std::filesystem::path &directory)
{
	if (!FirstWriteIn(directory))
		return;
	std::error_code error;
	for (std::filesystem::directory_iterator entry{directory, error};
	     !error && entry != std::filesystem::directory_iterator{}; entry.increment(error))
		if (std::error_code ignored; entry->path().filename() != target.filename() &&
		                             Abandoned(entry->path().filename().string()))
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

	// Only a partial this write made is removed on its failure; one an open found there is not its.
	bool made = false;
	const auto fail = [&](const std::string &problem)
	{
		std::error_code ignored;
		if (made)
			std::filesystem::remove(partial, ignored);
		return std::runtime_error{std::format("{} could not be written: {}", path.string(), problem)};
	};

	const std::filesystem::path directory =
	    path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
	CreateDirectoriesDurably(directory);

	{
		// The partial's name is new, so its open (O_EXCL) makes it with the mode given and never takes
		// a file a crash left, which the first write into the directory removes.
		RemoveAbandoned(path, directory);
		Descriptor file{::open(
		    partial.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
		    static_cast<mode_t>(permissions))};
		if (file.Get() < 0)
			throw fail(Problem("open"));
		made = true;
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
