#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
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
	// Bytes as they are, after their count: another archive inside this one, as a save game holds
	// the world's.
	void WriteBytes(std::span<const std::byte> bytes);
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

class BinaryReader;

// A reflected class's value as a binary archive wrote it, each field's bytes kept undecoded: what a
// migration of one value edits, as a component's chain does (Sim::SimWorld::RegisterComponent). A
// step reads a field removed or retyped with the reader's primitives (Open), by the old layout it
// knows, so the type that wrote it is never needed, and the record is then read as the type is now
// (ReadInto), its fields found by name as any value's are.
//
// A field's bytes hold names as indices into the archive's table (an enumerator, a nested class's
// fields), so a record shares that table with every record read from the same archive; a field set
// here adds to it whatever names its value uses.
class BinaryRecord
{
  public:
	struct Field
	{
		std::string name;
		std::vector<std::byte> bytes;
	};

	const std::vector<Field> &Fields() const
	{
		return m_fields;
	}
	bool Has(std::string_view name) const;
	// A reader over the field's bytes, which lives no longer than the record. Throws std::logic_error
	// for a field the record does not have.
	BinaryReader Open(std::string_view name) const;
	// The field read as a value of the type, the whole of it. Throws as Open does, and FormatError
	// for bytes that do not fit.
	template <typename T> T Get(std::string_view name) const;

	// Sets the field to the value, encoded as an archive encodes it, adding the field if the record
	// does not have it.
	void SetValue(std::string_view name, const entt::meta_any &value);
	template <typename T> void Set(std::string_view name, const T &value)
	{
		SetValue(name, entt::forward_as_meta(value));
	}
	// Throw std::logic_error for a field the record does not have, or, renaming, one it does.
	void Remove(std::string_view name);
	void Rename(std::string_view from, std::string_view to);

	// Reads the record into a value of the type it now is, as BinaryReader::ReadValue reads a class:
	// a field the type does not save is FormatError, and one the record leaves out keeps the value's.
	void ReadInto(entt::meta_any &value) const;
	template <typename T> void ReadInto(T &value) const
	{
		entt::meta_any reference = entt::forward_as_meta(value);
		ReadInto(reference);
	}

  private:
	friend class BinaryReader;

	std::vector<Field> m_fields;
	std::shared_ptr<std::vector<std::string>> m_pNames;
	// What errors name the record as: the format it was read from.
	std::string m_context;
};

class BinaryReader
{
  public:
	// Reads the header: throws FormatError for data that is not this format, and
	// FormatVersionError for another version.
	BinaryReader(std::span<const std::byte> bytes, std::string_view format, FormatVersion expected);
	// Data of an earlier version is migrated to the current one first (Migration.h), and the reader
	// reads the result, which it keeps. Throws FormatVersionError for a version the chain does not
	// reach.
	BinaryReader(std::span<const std::byte> bytes, const BinaryMigrations &migrations);
	~BinaryReader();
	BinaryReader(BinaryReader &&) noexcept;
	BinaryReader &operator=(BinaryReader &&) noexcept;
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
	// What WriteBytes wrote.
	std::vector<std::byte> ReadBytes();
	// A class's value, as WriteValue wrote it, left undecoded for a migration to edit. The records
	// one reader reads share one table of names.
	BinaryRecord ReadRecord();
	// The bytes not read yet: a bound on a count read from the data, before anything is
	// allocated for it.
	std::size_t Remaining() const;
	// Throws FormatError if bytes are left over.
	void End() const;
	// Throws FormatError naming the problem and where the reader is: for a caller's own checks.
	[[noreturn]] void Fail(std::string_view problem) const;

  private:
	friend class BinaryRecord;
	struct Impl;
	// Over a value alone, with no header: a record's field, whose names index the table given.
	BinaryReader(
	    std::span<const std::byte> bytes, const std::vector<std::string> &names,
	    std::string_view context);

	std::unique_ptr<Impl> m_pImpl;
};

template <typename T> T BinaryRecord::Get(std::string_view name) const
{
	BinaryReader reader = Open(name);
	T value{};
	reader.Read(value);
	reader.End();
	return value;
}
} // namespace Jettison::Serialise
