#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

namespace Jettison::Serialise
{
// Every format carries one. A reader that meets a version it does not know runs the migration
// chain (phase 8) or throws FormatVersionError naming both versions.
struct FormatVersion
{
	std::uint32_t version{1};
};

class FormatVersionError : public std::runtime_error
{
  public:
	FormatVersionError(std::string_view format, std::uint32_t found, std::uint32_t expected);
};

// Data that does not fit the type it is read as: a missing version, a field the type does not
// save, a value of the wrong kind, a truncated or overlong binary. The message names the type,
// the field path ("Cargo.holds[2].mass") and, for YAML, the line.
class FormatError : public std::runtime_error
{
  public:
	explicit FormatError(const std::string &message) : std::runtime_error{message}
	{
	}
};

// A type an archive cannot write or read: one not registered with Reflect, or a field of a type
// no codec handles. A defect in the calling code, not in the data.
class UnsupportedTypeError : public std::logic_error
{
  public:
	explicit UnsupportedTypeError(const std::string &message) : std::logic_error{message}
	{
	}
};

// Reflection-driven archives. The value is a class registered with Reflect; it is written field
// by field, Saved fields only, in registration order, and read back the same way. The loader and
// the writer are one pair, tested as a round trip (docs/history.md, row 8). YAML for authored
// content, binary for saves.
//
// A field's type is one of:
//   bool, the integer types, float, double, std::string;
//   glm::vec2/3/4 and glm::dvec2/3/4, written as a sequence of components;
//   entt::entity, written as its 32-bit identifier;
//   an enumeration registered with Reflect, written as the enumerator's name;
//   a class registered with Reflect, written as its Saved fields;
//   a std::vector of any of these.
//
// Reading starts from a default-constructed value, so a field the data leaves out keeps its
// default member initialiser. A field the data names but the type does not save is an error,
// not ignored: a misspelt key in authored content would otherwise vanish without a word.
//
// The version sits beside the root's fields in YAML (version: 1, then the fields, as the scene
// format already has it), so the root type cannot save a field named "version".
class YamlArchive
{
  public:
	static std::string Write(const entt::meta_any &value, FormatVersion version);
	static entt::meta_any Read(
	    std::string_view text, const entt::meta_type &type, FormatVersion expected);

	template <typename T> static std::string Write(const T &value, FormatVersion version)
	{
		return Write(entt::forward_as_meta(value), version);
	}
	template <typename T> static T Read(std::string_view text, FormatVersion expected)
	{
		entt::meta_any value = Read(text, entt::resolve<T>(), expected);
		return std::move(value.cast<T &>());
	}
};

// Little-endian, whatever the host: a header (magic, layout revision, version, the root type's
// name), a table of the names the file uses, then the value. Every field is written with its
// name's index in the table and its length in bytes, so a reader finds fields by name rather
// than by position, and a field whose type changed size is reported rather than misread.
// Enumerators are written by name too, so reordering an enumeration does not change a save.
class BinaryArchive
{
  public:
	static std::vector<std::byte> Write(const entt::meta_any &value, FormatVersion version);
	static entt::meta_any Read(
	    std::span<const std::byte> bytes, const entt::meta_type &type, FormatVersion expected);

	template <typename T> static std::vector<std::byte> Write(const T &value, FormatVersion version)
	{
		return Write(entt::forward_as_meta(value), version);
	}
	template <typename T> static T Read(std::span<const std::byte> bytes, FormatVersion expected)
	{
		entt::meta_any value = Read(bytes, entt::resolve<T>(), expected);
		return std::move(value.cast<T &>());
	}
};
} // namespace Jettison::Serialise
