#include <jettison/vfs/Pack.h>

#include <jettison/vfs/FileSystem.h>

#include <zstd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

#include "Source.h"

namespace Jettison::Vfs
{
namespace
{
constexpr std::array<char, 8> kMagic{'J', 'E', 'T', 'P', 'A', 'C', 'K', '\0'};
constexpr std::size_t kHeaderSize = 8 + 4 + 4 + 8 + 8;
// A record's fixed part: its path's length, its method, its offset and its two sizes.
constexpr std::size_t kRecordSize = 4 + 1 + 8 + 8 + 8;

enum class Method : std::uint8_t
{
	Stored = 0,
	Zstd = 1,
};

struct Entry
{
	std::string path;
	Method method{Method::Stored};
	std::uint64_t offset{0};
	std::uint64_t stored{0};
	std::uint64_t size{0};
};

// ---- Little-endian fields ------------------------------------------------------------------

void Put(std::vector<std::byte> &out, std::uint64_t value, std::size_t width)
{
	for (std::size_t byte = 0; byte < width; ++byte)
		out.push_back(static_cast<std::byte>((value >> (8 * byte)) & 0xff));
}

class Cursor
{
  public:
	explicit Cursor(std::span<const std::byte> bytes) : m_bytes{bytes}
	{
	}

	std::size_t Left() const
	{
		return m_bytes.size() - m_at;
	}
	// Nothing for a field that runs past the end.
	std::optional<std::uint64_t> Take(std::size_t width)
	{
		if (Left() < width)
			return std::nullopt;
		std::uint64_t value = 0;
		for (std::size_t byte = 0; byte < width; ++byte)
			value |= static_cast<std::uint64_t>(m_bytes[m_at + byte]) << (8 * byte);
		m_at += width;
		return value;
	}
	std::optional<std::string_view> Text(std::size_t length)
	{
		if (Left() < length)
			return std::nullopt;
		const std::string_view text{reinterpret_cast<const char *>(m_bytes.data() + m_at), length};
		m_at += length;
		return text;
	}

  private:
	std::span<const std::byte> m_bytes;
	std::size_t m_at{0};
};

// ---- Reading -----------------------------------------------------------------------------------

// The pack's file stays open for the run, so a pack replaced on the disk while it is mounted is
// read as it was, rather than at the old offsets of the new one. Reads are pread, which keeps no
// position, so the main thread and the I/O worker read at once.
class PackSource final : public Detail::Source
{
  public:
	explicit PackSource(const std::filesystem::path &pack) : m_name{pack.string()}
	{
		m_descriptor = ::open(pack.c_str(), O_RDONLY | O_CLOEXEC);
		if (m_descriptor < 0)
			throw std::runtime_error{std::format(
			    "the pack {} could not be opened: {}", m_name, std::generic_category().message(errno))};
		try
		{
			struct stat status{};
			if (::fstat(m_descriptor, &status) != 0)
				Fail(std::format("it could not be read: {}", std::generic_category().message(errno)));
			m_fileSize = static_cast<std::uint64_t>(status.st_size);
			std::error_code error;
			m_time = std::filesystem::last_write_time(pack, error);
			ReadIndex();
		}
		catch (...)
		{
			::close(m_descriptor);
			throw;
		}
	}
	~PackSource() override
	{
		::close(m_descriptor);
	}
	PackSource(const PackSource &) = delete;
	PackSource &operator=(const PackSource &) = delete;

	std::optional<FileInfo> Find(std::string_view virtualPath) const override
	{
		const Entry *entry = Lookup(virtualPath);
		if (!entry)
			return std::nullopt;
		return FileInfo{0, m_time, entry->size};
	}

	std::optional<std::vector<std::byte>> Read(std::string_view virtualPath) const override
	{
		const Entry *entry = Lookup(virtualPath);
		if (!entry)
			return std::nullopt;
		std::vector<std::byte> stored = ReadRange(entry->offset, entry->stored, entry->path);
		if (entry->method == Method::Stored)
			return stored;

		const unsigned long long declared = ZSTD_getFrameContentSize(stored.data(), stored.size());
		if (declared != entry->size)
			throw std::runtime_error{std::format(
			    "Vfs: {} in the pack {} is damaged: its frame does not hold the size the index gives",
			    entry->path, m_name)};
		std::vector<std::byte> bytes(entry->size);
		const std::size_t written =
		    ZSTD_decompress(bytes.data(), bytes.size(), stored.data(), stored.size());
		if (ZSTD_isError(written) || written != bytes.size())
			throw std::runtime_error{std::format(
			    "Vfs: {} in the pack {} is damaged: {}", entry->path, m_name,
			    ZSTD_isError(written) ? ZSTD_getErrorName(written) : "it is short")};
		return bytes;
	}

	void List(std::string_view virtualDirectory, std::vector<std::string> &paths) const override
	{
		if (virtualDirectory.empty())
		{
			for (const Entry &entry : m_entries)
				paths.push_back(entry.path);
			return;
		}
		const std::string prefix = std::string{virtualDirectory} + '/';
		auto entry = std::ranges::lower_bound(m_entries, prefix, {}, &Entry::path);
		for (; entry != m_entries.end() && entry->path.starts_with(prefix); ++entry)
			paths.push_back(entry->path);
	}

  private:
	[[noreturn]] void Fail(std::string_view problem) const
	{
		throw std::runtime_error{std::format("{} is not a pack this build reads: {}", m_name, problem)};
	}

	const Entry *Lookup(std::string_view virtualPath) const
	{
		const auto entry = std::ranges::lower_bound(m_entries, virtualPath, {}, &Entry::path);
		return entry != m_entries.end() && entry->path == virtualPath ? &*entry : nullptr;
	}

	std::vector<std::byte> ReadRange(
	    std::uint64_t offset, std::uint64_t size, std::string_view what) const
	{
		std::vector<std::byte> bytes(size);
		std::size_t done = 0;
		while (done < bytes.size())
		{
			const ssize_t count = ::pread(
			    m_descriptor, bytes.data() + done, bytes.size() - done,
			    static_cast<off_t>(offset + done));
			if (count < 0 && errno == EINTR)
				continue;
			if (count <= 0)
				throw std::runtime_error{std::format(
				    "Vfs: {} in the pack {} could not be read: {}", what, m_name,
				    count < 0 ? std::generic_category().message(errno) : "the pack is short")};
			done += static_cast<std::size_t>(count);
		}
		return bytes;
	}

	void ReadIndex()
	{
		if (m_fileSize < kHeaderSize)
			Fail("it is shorter than a header");
		const std::vector<std::byte> header = ReadRange(0, kHeaderSize, "the header");
		if (std::memcmp(header.data(), kMagic.data(), kMagic.size()) != 0)
			Fail("it does not begin as a pack does");
		Cursor fields{std::span{header}.subspan(kMagic.size())};
		const std::uint64_t layout = *fields.Take(4);
		const std::uint64_t count = *fields.Take(4);
		const std::uint64_t indexOffset = *fields.Take(8);
		const std::uint64_t indexSize = *fields.Take(8);
		if (layout != kPackLayout)
			Fail(std::format("its layout is {}, and this build reads {}", layout, kPackLayout));
		if (indexOffset < kHeaderSize || indexOffset > m_fileSize ||
		    indexSize != m_fileSize - indexOffset)
			Fail("its index is not where its header says, at its end");
		if (count > indexSize / kRecordSize)
			Fail("its index is too short for the files its header counts");

		const std::vector<std::byte> index = ReadRange(indexOffset, indexSize, "the index");
		Cursor records{index};
		m_entries.reserve(count);
		for (std::uint64_t record = 0; record < count; ++record)
		{
			const std::optional<std::uint64_t> length = records.Take(4);
			const std::optional<std::string_view> path =
			    length ? records.Text(*length) : std::optional<std::string_view>{};
			if (!path)
				Fail(std::format("its index ends inside file {}", record));
			if (!FileSystem::IsValidPath(*path))
				Fail(
				    std::format(
				        "file {} has the path '{}', which is not a virtual path", record, *path));
			if (!m_entries.empty() && !(m_entries.back().path < *path))
				Fail(std::format("its index is not sorted by path, each once, at '{}'", *path));
			const std::optional<std::uint64_t> method = records.Take(1);
			const std::optional<std::uint64_t> offset = records.Take(8);
			const std::optional<std::uint64_t> stored = records.Take(8);
			const std::optional<std::uint64_t> size = records.Take(8);
			if (!size)
				Fail(std::format("its index ends inside the record of '{}'", *path));
			if (*method > static_cast<std::uint64_t>(Method::Zstd))
				Fail(
				    std::format(
				        "'{}' is stored by method {}, which this build does not know", *path, *method));
			if (*offset < kHeaderSize || *offset > indexOffset || *stored > indexOffset - *offset)
				Fail(std::format("the bytes of '{}' are not between its header and its index", *path));
			if (static_cast<Method>(*method) == Method::Stored && *stored != *size)
				Fail(std::format("'{}' is stored as it is, at a size other than its own", *path));
			if (*size > std::numeric_limits<std::size_t>::max())
				Fail(std::format("'{}' is larger than this build can hold", *path));
			m_entries.push_back(
			    {std::string{*path}, static_cast<Method>(*method), *offset, *stored, *size});
		}
		if (records.Left() != 0)
			Fail("its index runs on past the files its header counts");
	}

	std::string m_name;
	int m_descriptor{-1};
	std::uint64_t m_fileSize{0};
	std::filesystem::file_time_type m_time{};
	// Sorted by path.
	std::vector<Entry> m_entries;
};

// ---- Writing -----------------------------------------------------------------------------------

std::vector<std::byte> ReadWhole(const std::filesystem::path &path)
{
	std::ifstream file{path, std::ios::binary | std::ios::ate};
	const std::streamoff size = file ? static_cast<std::streamoff>(file.tellg()) : -1;
	if (size < 0)
		throw std::runtime_error{std::format("Vfs: {} could not be opened", path.string())};
	std::vector<std::byte> bytes(static_cast<std::size_t>(size));
	file.seekg(0);
	file.read(reinterpret_cast<char *>(bytes.data()), size);
	if (file.gcount() != size)
		throw std::runtime_error{std::format("Vfs: {} could not be read", path.string())};
	return bytes;
}

struct ContextDeleter
{
	void operator()(ZSTD_CCtx *context) const
	{
		ZSTD_freeCCtx(context);
	}
};

// One frame of the bytes at the level, with their size and checksum in it.
std::vector<std::byte> Compress(ZSTD_CCtx &context, std::span<const std::byte> bytes, int level)
{
	ZSTD_CCtx_reset(&context, ZSTD_reset_session_and_parameters);
	ZSTD_CCtx_setParameter(&context, ZSTD_c_compressionLevel, level);
	ZSTD_CCtx_setParameter(&context, ZSTD_c_checksumFlag, 1);
	ZSTD_CCtx_setParameter(&context, ZSTD_c_contentSizeFlag, 1);
	std::vector<std::byte> frame(ZSTD_compressBound(bytes.size()));
	const std::size_t size =
	    ZSTD_compress2(&context, frame.data(), frame.size(), bytes.data(), bytes.size());
	if (ZSTD_isError(size))
		throw std::runtime_error{std::format("Vfs: zstd failed: {}", ZSTD_getErrorName(size))};
	frame.resize(size);
	return frame;
}
} // namespace

std::shared_ptr<const Detail::Source> Detail::MakePackSource(const std::filesystem::path &pack)
{
	return std::make_shared<PackSource>(pack);
}

PackSummary WritePack(
    const std::filesystem::path &directory, const std::filesystem::path &pack, PackOptions options)
{
	if (options.level < 1 || options.level > ZSTD_maxCLevel())
		throw std::invalid_argument{std::format(
		    "Vfs: the pack's level is {}, and zstd's run from 1 to {}", options.level,
		    ZSTD_maxCLevel())};
	std::error_code error;
	if (!std::filesystem::is_directory(directory, error))
		throw std::runtime_error{std::format("Vfs: {} is not a directory to pack", directory.string())};

	std::vector<std::filesystem::path> roots;
	for (const std::string &subdirectory : options.directories)
	{
		if (!FileSystem::IsValidPath(subdirectory))
			throw std::invalid_argument{
			    std::format("Vfs: '{}' is not a virtual path to pack", subdirectory)};
		roots.push_back(directory / subdirectory);
		if (!std::filesystem::is_directory(roots.back(), error))
			throw std::runtime_error{
			    std::format("Vfs: {} is not a directory to pack", roots.back().string())};
	}
	if (roots.empty())
		roots.push_back(directory);

	PackSummary summary;
	std::vector<std::pair<std::string, std::filesystem::path>> files;
	for (const std::filesystem::path &root : roots)
	{
		for (auto entry =
		         std::filesystem::recursive_directory_iterator{
		             root, std::filesystem::directory_options::follow_directory_symlink, error};
		     !error && entry != std::filesystem::recursive_directory_iterator{}; entry.increment(error))
		{
			std::error_code entryError;
			if (!entry->is_regular_file(entryError))
				continue;
			std::string path = entry->path().lexically_relative(directory).generic_string();
			if (FileSystem::IsValidPath(path))
				files.emplace_back(std::move(path), entry->path());
			else
				summary.skipped.push_back(std::move(path));
		}
		if (error)
			throw std::runtime_error{
			    std::format("Vfs: {} could not be listed: {}", root.string(), error.message())};
	}
	// Directories named twice, or one inside another, pack their files once.
	std::ranges::sort(files);
	const auto repeated =
	    std::ranges::unique(files, {}, &std::pair<std::string, std::filesystem::path>::first);
	files.erase(repeated.begin(), repeated.end());
	std::ranges::sort(summary.skipped);
	if (files.size() > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error{
		    std::format("Vfs: {} holds more files than a pack can", directory.string())};

	const std::filesystem::path partial = pack.string() + ".partial";
	if (pack.has_parent_path())
		std::filesystem::create_directories(pack.parent_path(), error);
	std::ofstream out{partial, std::ios::binary | std::ios::trunc};
	if (!out)
		throw std::runtime_error{std::format("Vfs: {} could not be written", partial.string())};
	// The header is written again once the index's place is known.
	const std::vector<std::byte> blank(kHeaderSize);
	out.write(reinterpret_cast<const char *>(blank.data()), static_cast<std::streamsize>(blank.size()));

	const std::unique_ptr<ZSTD_CCtx, ContextDeleter> context{ZSTD_createCCtx()};
	if (!context)
		throw std::runtime_error{"Vfs: zstd could not make a context"};
	std::vector<std::byte> index;
	std::uint64_t offset = kHeaderSize;
	for (const auto &[path, source] : files)
	{
		std::vector<std::byte> bytes = ReadWhole(source);
		const std::uint64_t size = bytes.size();
		Method method = Method::Stored;
		std::vector<std::byte> stored;
		// Level 1 is the probe: a file it cannot shrink by a thirty-second is stored as it is.
		if (std::vector<std::byte> trial = Compress(*context, bytes, 1);
		    !bytes.empty() && trial.size() + bytes.size() / 32 <= bytes.size())
		{
			method = Method::Zstd;
			stored = options.level == 1 ? std::move(trial) : Compress(*context, bytes, options.level);
			++summary.compressed;
		}
		else
			stored = std::move(bytes);

		Put(index, path.size(), 4);
		for (const char character : path)
			index.push_back(static_cast<std::byte>(character));
		Put(index, static_cast<std::uint64_t>(method), 1);
		Put(index, offset, 8);
		Put(index, stored.size(), 8);
		Put(index, size, 8);
		out.write(
		    reinterpret_cast<const char *>(stored.data()), static_cast<std::streamsize>(stored.size()));
		offset += stored.size();
		summary.bytes += size;
		++summary.files;
	}
	out.write(reinterpret_cast<const char *>(index.data()), static_cast<std::streamsize>(index.size()));

	std::vector<std::byte> header{
	    reinterpret_cast<const std::byte *>(kMagic.data()),
	    reinterpret_cast<const std::byte *>(kMagic.data()) + kMagic.size()};
	Put(header, kPackLayout, 4);
	Put(header, files.size(), 4);
	Put(header, offset, 8);
	Put(header, index.size(), 8);
	out.seekp(0);
	out.write(
	    reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
	out.close();
	summary.packedBytes = offset + index.size();
	if (!out)
	{
		std::filesystem::remove(partial, error);
		throw std::runtime_error{std::format("Vfs: {} could not be written", partial.string())};
	}
	std::filesystem::rename(partial, pack, error);
	if (error)
	{
		std::filesystem::remove(partial, error);
		throw std::runtime_error{
		    std::format("Vfs: {} could not be put in place: {}", pack.string(), error.message())};
	}
	return summary;
}
} // namespace Jettison::Vfs
