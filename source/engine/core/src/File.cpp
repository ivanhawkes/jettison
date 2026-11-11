#include <jettison/core/File.h>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

namespace Jettison::Core
{
namespace
{
// A descriptor closed however the write leaves.
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
} // namespace

void WriteFileDurably(const std::filesystem::path &path, std::span<const std::byte> bytes)
{
	std::filesystem::path partial = path;
	partial += kPartialSuffix;
	const auto fail = [&](const std::string &problem)
	{
		std::error_code ignored;
		std::filesystem::remove(partial, ignored);
		return std::runtime_error{std::format("{} could not be written: {}", path.string(), problem)};
	};

	std::error_code error;
	const std::filesystem::path directory =
	    path.has_parent_path() ? path.parent_path() : std::filesystem::path{"."};
	std::filesystem::create_directories(directory, error);
	if (error)
		throw fail(error.message());

	{
		Descriptor file{::open(partial.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644)};
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
	// rename replaces the old file in one step on POSIX.
	std::filesystem::rename(partial, path, error);
	if (error)
		throw fail(error.message());

	// The file is complete under its name; what remains is the directory's entry for it. A failure
	// here leaves the new file in place, which is reported, since it may not survive a power cut.
	Descriptor parent{::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)};
	if (parent.Get() < 0 || ::fsync(parent.Get()) != 0)
		throw std::runtime_error{std::format(
		    "{} was written, but its directory could not be flushed: {}", path.string(),
		    std::strerror(errno))};
}

void WriteFileDurably(const std::filesystem::path &path, std::string_view text)
{
	WriteFileDurably(path, std::as_bytes(std::span{text.data(), text.size()}));
}
} // namespace Jettison::Core
