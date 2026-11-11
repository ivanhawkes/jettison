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

namespace YAML
{
class Node;
}

namespace Jettison::Serialise
{
// Every format carries one. A reader that meets another version runs the format's migrations
// (Migration.h) when it is given them, and throws FormatVersionError naming both versions when the
// chain does not reach the one found, or when it is given none.
struct FormatVersion
{
	std::uint32_t version{1};
};

class FormatVersionError : public std::runtime_error
{
  public:
	// The reason, when there is one to give: "it was written by a later build".
	FormatVersionError(
	    std::string_view format, std::uint32_t found, std::uint32_t expected,
	    std::string_view reason = {});

	std::uint32_t Found() const
	{
		return m_found;
	}
	std::uint32_t Expected() const
	{
		return m_expected;
	}

  private:
	std::uint32_t m_found;
	std::uint32_t m_expected;
};

template <typename Data> class Migrations;
struct BinaryDocument;
using BinaryMigrations = Migrations<BinaryDocument>;

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
// the writer are one pair, tested as a round trip: the scene's loader once refused a file its own
// writer had produced. YAML for authored content, binary for saves.
//
// A field's type is one of:
//   bool, the integer types, float, double, std::string;
//   glm::vec2/3/4 and glm::dvec2/3/4, written as a sequence of components;
//   glm::quat and glm::dquat, written as x, y, z, w (GLM's storage order);
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
	// Text of an earlier version is migrated first, as a YAML tree; what a step leaves is read as
	// text of the current version would be. Errors name the lines of the text as it was.
	static entt::meta_any Read(
	    std::string_view text, const entt::meta_type &type, const Migrations<YAML::Node> &migrations);

	template <typename T> static std::string Write(const T &value, FormatVersion version)
	{
		return Write(entt::forward_as_meta(value), version);
	}
	template <typename T> static T Read(std::string_view text, FormatVersion expected)
	{
		entt::meta_any value = Read(text, entt::resolve<T>(), expected);
		return std::move(value.cast<T &>());
	}
	template <typename T> static T Read(std::string_view text, const Migrations<YAML::Node> &migrations)
	{
		entt::meta_any value = Read(text, entt::resolve<T>(), migrations);
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
	// Data of an earlier version is migrated first. The chain's format is the root type's name.
	static entt::meta_any Read(
	    std::span<const std::byte> bytes, const entt::meta_type &type,
	    const BinaryMigrations &migrations);

	template <typename T> static std::vector<std::byte> Write(const T &value, FormatVersion version)
	{
		return Write(entt::forward_as_meta(value), version);
	}
	template <typename T> static T Read(std::span<const std::byte> bytes, FormatVersion expected)
	{
		entt::meta_any value = Read(bytes, entt::resolve<T>(), expected);
		return std::move(value.cast<T &>());
	}
	template <typename T>
	static T Read(std::span<const std::byte> bytes, const BinaryMigrations &migrations)
	{
		entt::meta_any value = Read(bytes, entt::resolve<T>(), migrations);
		return std::move(value.cast<T &>());
	}
};
} // namespace Jettison::Serialise
