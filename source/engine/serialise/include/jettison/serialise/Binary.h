#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include <jettison/serialise/Archive.h>

namespace Jettison::Serialise
{
// The streams BinaryArchive is made of, for a format that is not one reflected value: a world
// save is a registry, written as a sequence of values. The layout is BinaryArchive's (Archive.h)
// with the format's name where the root type's name would be. A value is anything an archive
// writes: a registered class, a leaf, an enumeration, a vector; it is written with the same
// encoding, its fields found by name on the way back.
class BinaryWriter
{
  public:
	// The format's name goes in the header and in errors.
	explicit BinaryWriter(std::string_view format);
	~BinaryWriter();
	BinaryWriter(const BinaryWriter &) = delete;
	BinaryWriter &operator=(const BinaryWriter &) = delete;

	// Width is 1, 2, 4 or 8 bytes, little-endian.
	void WriteUnsigned(std::uint64_t value, std::size_t width);
	// Text from the archive's name table: written once however often it is used.
	void WriteName(std::string_view name);
	void WriteValue(const entt::meta_any &value);
	template <typename T> void Write(const T &value)
	{
		WriteValue(entt::forward_as_meta(value));
	}

	// The header and name table, then everything written. The writer is spent.
	std::vector<std::byte> Finish(FormatVersion version);

  private:
	struct Impl;
	std::unique_ptr<Impl> m_pImpl;
};

class BinaryReader
{
  public:
	// Reads the header: throws FormatError for data that is not this format, and
	// FormatVersionError for another version.
	BinaryReader(std::span<const std::byte> bytes, std::string_view format, FormatVersion expected);
	~BinaryReader();
	BinaryReader(const BinaryReader &) = delete;
	BinaryReader &operator=(const BinaryReader &) = delete;

	std::uint64_t ReadUnsigned(std::size_t width);
	// The view lives as long as the reader.
	std::string_view ReadName();
	// Reads into a value of the type the writer wrote, owned or a reference (forward_as_meta).
	// A class keeps whatever the data leaves out, as Archive.h describes.
	void ReadValue(entt::meta_any &value);
	template <typename T> void Read(T &value)
	{
		entt::meta_any reference = entt::forward_as_meta(value);
		ReadValue(reference);
	}
	// The bytes not read yet: a bound on a count read from the data, before anything is
	// allocated for it.
	std::size_t Remaining() const;
	// Throws FormatError if bytes are left over.
	void End() const;
	// Throws FormatError naming the problem and where the reader is: for a caller's own checks.
	[[noreturn]] void Fail(std::string_view problem) const;

  private:
	struct Impl;
	std::unique_ptr<Impl> m_pImpl;
};
} // namespace Jettison::Serialise
