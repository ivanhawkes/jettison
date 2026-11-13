#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Script
{
// What a script sees of the game's types is generated from the reflection tables
// (docs/architecture.md, "Scripts"). A type registered with Reflect, with Attribute::Script, is laid
// out in a script's linear memory as a C compiler for wasm32 lays out the struct the header
// (Header) declares for it: its fields in registration order, each at its own alignment, the
// struct padded to its widest. The host copies an event into that layout for a handler, and a
// command out of it for the queue, so a script reads and writes plain C structs and nothing crosses
// the boundary but bytes.
//
// A field is laid out as:
//   - a bool, an integer, a float or a double: itself;
//   - a GLM vector or quaternion: its components, as jettison_vec3 and the rest;
//   - an entity: its 32-bit identifier, jettison_entity;
//   - an enumeration registered with Reflect: an unsigned integer of its size, with the
//     enumerators' values as constants;
//   - a std::string: jettison_string, a pointer and a size, the bytes elsewhere in memory;
//   - a class registered with Reflect: its own struct, nested;
//   - a std::vector of any of these: a pointer to its elements and their count.
// Anything else (a std::optional, a map) is left out of the layout, and the header says so: a
// script does not see it, and a command read from a script leaves it at its default.

// What a layout cannot be made for, or what a script's memory holds that does not read as its
// type: a pointer past the end, a bool that is neither 0 nor 1.
class BindingError : public std::runtime_error
{
  public:
	explicit BindingError(const std::string &message) : std::runtime_error{message}
	{
	}
};

enum class ValueKind : std::uint8_t
{
	Bool,
	Signed,
	Unsigned,
	Float,
	Entity,
	Enum,
	String,
	Struct,
	Array,
};

struct TypeLayout;

// How one value is held: its kind, its size and alignment in a script's memory, and for a leaf its
// components (one for a scalar, three for a glm::dvec3), each of a width in bytes.
struct ValueLayout
{
	ValueKind kind{ValueKind::Struct};
	std::uint32_t size{0};
	std::uint32_t align{1};
	std::uint8_t width{0};
	std::uint8_t count{1};
	// The value's own type, as the engine holds it.
	entt::meta_type type;
	// The C name the header gives it: "int64_t", "jettison_dvec3", "Shell_Cargo".
	std::string cName;
	// For a Struct, its type's layout; for an Array, its element's.
	const TypeLayout *pStruct{nullptr};
	std::shared_ptr<const ValueLayout> element;
};

struct FieldLayout
{
	entt::meta_data field;
	std::string name;
	std::uint32_t offset{0};
	ValueLayout value;
};

struct TypeLayout
{
	entt::meta_type type;
	// The registered name, "Shell.Traded", and the header's, "Shell_Traded".
	std::string name;
	std::string cName;
	std::uint32_t size{0};
	std::uint32_t align{1};
	std::vector<FieldLayout> fields;
	// The fields a script does not see, by name, with the reason.
	std::vector<std::string> omitted;
};

// The layouts of the types scripts see, made from the reflection tables the first time each is
// asked for and kept for the run; registration is over by the time any script loads. Not
// thread-safe, as the tables' readers are not.
class Bindings
{
  public:
	// Throws BindingError for a type that is not a class registered with Reflect, or one that holds
	// itself.
	const TypeLayout &Layout(const entt::meta_type &type);

	// The bytes a value needs in a script's memory: its struct, then what its strings and arrays
	// point at, each at its alignment. What Write fills.
	std::uint32_t SizeOf(const TypeLayout &layout, const void *pObject) const;
	// Writes the object into memory at offset, its struct first and the bytes its fields point at
	// after it, within SizeOf(layout, pObject) bytes. Pointers are offsets in memory, as a script
	// sees them. Throws BindingError for a value that does not fit: an enumerator not registered, or
	// a size past 32 bits.
	void Write(
	    const TypeLayout &layout, const void *pObject, std::span<std::byte> memory,
	    std::uint32_t offset) const;
	// Reads the struct at offset in memory into the object, which is of the layout's type. Fields
	// the layout leaves out keep their values. Throws BindingError naming the field for a pointer or
	// a struct out of memory's bounds, a bool that is not 0 or 1, or an enumerator not registered;
	// the object may then be part written.
	void Read(
	    const TypeLayout &layout, std::span<const std::byte> memory, std::uint32_t offset,
	    void *pObject) const;

  private:
	ValueLayout ValueOf(const entt::meta_type &type, std::string_view path);

	// Keyed by EnTT's type hash; looked up and never iterated for an outcome.
	std::map<entt::id_type, std::unique_ptr<TypeLayout>> m_layouts;
	// The types being laid out, to refuse one that holds itself.
	std::vector<entt::id_type> m_making;
};

// The C header a script author compiles against: the host's imports, every type given (and every
// type their fields hold), in name order, each with its size and offsets asserted, and an import
// to send each command given. Commands are among the types; each is declared once. Generated, so
// the same tables give the same text.
std::string Header(
    Bindings &bindings, std::span<const entt::meta_type> types,
    std::span<const entt::meta_type> commands);

// The same, as a Rust module (jettison_script.rs) for a script written in Rust: the same names, the
// same layouts asserted, the imports in an extern block, and a safe function over each.
std::string RustModule(
    Bindings &bindings, std::span<const entt::meta_type> types,
    std::span<const entt::meta_type> commands);

// The object a meta_any holds: what the layouts read and write through. The mutable one is for an
// any that owns its object or refers to it mutably, as a value the host constructs does.
inline const void *ObjectOf(const entt::meta_any &value)
{
	return value.base().data();
}
inline void *ObjectOf(entt::meta_any &value)
{
	return const_cast<void *>(value.base().data());
}

// The import a script sends a command of the type with, which is also the capability that grants
// it: "send:Shell.Trade".
std::string SendImport(const entt::meta_type &command);
} // namespace Jettison::Script
