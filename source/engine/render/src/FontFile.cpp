#include <jettison/render/FontFile.h>

#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>

namespace Jettison::Render
{
namespace
{
// The largest face read. The engine's largest is Hack's bold italic, 315 KiB, and a face with the
// whole of a script such as Japanese is tens of times that, which this holds; the file is read whole
// and FreeType keeps it, so the size is a bound on what a face costs.
constexpr std::size_t kMaxFontBytes = std::size_t{32} * 1024 * 1024;

// The sfnt versions FreeType reads a face from (the OpenType specification's "Organization of an
// OpenType Font"; "true" is Apple's TrueType), and a collection's tag.
constexpr std::uint32_t kTrueType = 0x00010000;
constexpr std::uint32_t kOpenTypeCff = 0x4f54544f;   // "OTTO"
constexpr std::uint32_t kAppleTrueType = 0x74727565; // "true"
constexpr std::uint32_t kCollection = 0x74746366;    // "ttcf"

// A font's table directory: a 12-byte header with its table count at 4, then 16 bytes a table, its
// offset at 8 and its length at 12. A collection's header has its font count at 8 and their
// directories' offsets after it, 4 bytes each.
constexpr std::size_t kDirectoryBytes = 12;
constexpr std::size_t kTableCountAt = 4;
constexpr std::size_t kTableRecordBytes = 16;
constexpr std::size_t kTableOffsetAt = 8;
constexpr std::size_t kTableLengthAt = 12;
constexpr std::size_t kCollectionCountAt = 8;
constexpr std::size_t kCollectionOffsetsAt = 12;
constexpr std::size_t kOffsetBytes = 4;
constexpr unsigned kBitsPerByte = 8;


// Big-endian, as every number in an sfnt is; the caller has checked the bytes are there.
std::uint32_t BigEndian(std::span<const std::byte> bytes, std::size_t at, std::size_t width)
{
	std::uint32_t value = 0;
	for (std::size_t index = 0; index < width; ++index)
		value = (value << kBitsPerByte) | std::to_integer<std::uint32_t>(bytes[at + index]);
	return value;
}


[[noreturn]] void Refuse(std::string_view name, std::string_view problem)
{
	throw std::runtime_error(std::format("The font '{}' {}", name, problem));
}


void CheckDirectory(std::span<const std::byte> bytes, std::uint64_t at, std::string_view name)
{
	if (at > bytes.size() || bytes.size() - at < kDirectoryBytes)
		Refuse(name, "has a table directory past its end");
	const auto start = static_cast<std::size_t>(at);
	const std::uint32_t version = BigEndian(bytes, start, sizeof(std::uint32_t));
	if (version != kTrueType && version != kOpenTypeCff && version != kAppleTrueType)
		Refuse(name, "is not a TrueType or OpenType font");
	const std::uint32_t tables = BigEndian(bytes, start + kTableCountAt, sizeof(std::uint16_t));
	if (tables == 0 || (bytes.size() - start - kDirectoryBytes) / kTableRecordBytes < tables)
		Refuse(name, std::format("lists {} tables, which its directory does not hold", tables));
	for (std::size_t table = 0; table < tables; ++table)
	{
		const std::size_t record = start + kDirectoryBytes + table * kTableRecordBytes;
		const std::uint64_t offset = BigEndian(bytes, record + kTableOffsetAt, sizeof(std::uint32_t));
		const std::uint64_t length = BigEndian(bytes, record + kTableLengthAt, sizeof(std::uint32_t));
		if (offset + length > bytes.size())
			Refuse(name, std::format("has a table of {} bytes at {}, past its end", length, offset));
	}
}
} // namespace


void CheckFontFile(std::span<const std::byte> bytes, std::string_view name)
{
	if (bytes.size() > kMaxFontBytes)
		Refuse(
		    name,
		    std::format("is {} bytes, more than the {} a font may be", bytes.size(), kMaxFontBytes));
	if (bytes.size() < kDirectoryBytes)
		Refuse(name, "is not a TrueType or OpenType font");
	if (BigEndian(bytes, 0, sizeof(std::uint32_t)) != kCollection)
	{
		CheckDirectory(bytes, 0, name);
		return;
	}
	const std::uint32_t fonts = BigEndian(bytes, kCollectionCountAt, sizeof(std::uint32_t));
	if (fonts == 0 || (bytes.size() - kCollectionOffsetsAt) / kOffsetBytes < fonts)
		Refuse(name, std::format("is a collection of {} fonts, which its header does not hold", fonts));
	for (std::size_t font = 0; font < fonts; ++font)
		CheckDirectory(
		    bytes, BigEndian(bytes, kCollectionOffsetsAt + font * kOffsetBytes, sizeof(std::uint32_t)),
		    name);
}
} // namespace Jettison::Render
