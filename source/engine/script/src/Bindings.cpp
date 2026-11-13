#include <jettison/script/Bindings.h>

#include <jettison/reflect/Reflect.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <limits>
#include <set>
#include <type_traits>
#include <unordered_map>

namespace Jettison::Script
{
// A script's memory is little-endian, and the host copies leaves into it byte for byte.
static_assert(std::endian::native == std::endian::little);

namespace
{
// A wasm32 pointer and a count, as jettison_string and every array are.
constexpr std::uint32_t kSpanSize = 8;
constexpr std::uint32_t kSpanAlign = 4;

std::uint32_t AlignUp(std::uint64_t value, std::uint32_t align)
{
	const std::uint64_t aligned = (value + align - 1) / align * align;
	if (aligned > std::numeric_limits<std::uint32_t>::max())
		throw BindingError{"Script: a value is larger than a script's 4 GiB of memory"};
	return static_cast<std::uint32_t>(aligned);
}

std::string CName(std::string_view registered)
{
	std::string name{registered};
	std::ranges::replace(name, '.', '_');
	return name;
}

struct Leaf
{
	ValueKind kind;
	std::uint8_t width;
	std::uint8_t count;
	const char *cName;
};

template <typename T> ValueKind ScalarKind()
{
	if constexpr (std::is_same_v<T, bool>)
		return ValueKind::Bool;
	else if constexpr (std::is_floating_point_v<T>)
		return ValueKind::Float;
	else if constexpr (std::is_signed_v<T>)
		return ValueKind::Signed;
	else
		return ValueKind::Unsigned;
}

template <typename T, typename Component, std::uint8_t Count> Leaf Make(const char *cName)
{
	static_assert(sizeof(T) == sizeof(Component) * Count && std::is_trivially_copyable_v<T>);
	return {ScalarKind<Component>(), sizeof(Component), Count, cName};
}

template <typename T> const char *IntegerName()
{
	if constexpr (std::is_signed_v<T>)
		return sizeof(T) == 1   ? "int8_t"
		       : sizeof(T) == 2 ? "int16_t"
		       : sizeof(T) == 4 ? "int32_t"
		                        : "int64_t";
	else
		return sizeof(T) == 1   ? "uint8_t"
		       : sizeof(T) == 2 ? "uint16_t"
		       : sizeof(T) == 4 ? "uint32_t"
		                        : "uint64_t";
}

template <typename T> void AddInteger(std::unordered_map<entt::id_type, Leaf> &table)
{
	table.emplace(entt::type_id<T>().hash(), Make<T, T, 1>(IntegerName<T>()));
}

// Lookups only: no outcome depends on this map's order. Every standard integer type by name, as the
// archives do, since std::int64_t is long here and long long elsewhere.
const std::unordered_map<entt::id_type, Leaf> &Leaves()
{
	static const std::unordered_map<entt::id_type, Leaf> leaves = []
	{
		std::unordered_map<entt::id_type, Leaf> table;
		table.emplace(entt::type_id<bool>().hash(), Make<bool, bool, 1>("bool"));
		AddInteger<signed char>(table);
		AddInteger<short>(table);
		AddInteger<int>(table);
		AddInteger<long>(table);
		AddInteger<long long>(table);
		AddInteger<unsigned char>(table);
		AddInteger<unsigned short>(table);
		AddInteger<unsigned int>(table);
		AddInteger<unsigned long>(table);
		AddInteger<unsigned long long>(table);
		table.emplace(entt::type_id<float>().hash(), Make<float, float, 1>("float"));
		table.emplace(entt::type_id<double>().hash(), Make<double, double, 1>("double"));
		table.emplace(entt::type_id<glm::vec2>().hash(), Make<glm::vec2, float, 2>("jettison_vec2"));
		table.emplace(entt::type_id<glm::vec3>().hash(), Make<glm::vec3, float, 3>("jettison_vec3"));
		table.emplace(entt::type_id<glm::vec4>().hash(), Make<glm::vec4, float, 4>("jettison_vec4"));
		table.emplace(
		    entt::type_id<glm::dvec2>().hash(), Make<glm::dvec2, double, 2>("jettison_dvec2"));
		table.emplace(
		    entt::type_id<glm::dvec3>().hash(), Make<glm::dvec3, double, 3>("jettison_dvec3"));
		table.emplace(
		    entt::type_id<glm::dvec4>().hash(), Make<glm::dvec4, double, 4>("jettison_dvec4"));
		// x, y, z then w: GLM's storage order, as the archives hold a quaternion.
		table.emplace(entt::type_id<glm::quat>().hash(), Make<glm::quat, float, 4>("jettison_quat"));
		table.emplace(
		    entt::type_id<glm::dquat>().hash(), Make<glm::dquat, double, 4>("jettison_dquat"));
		static_assert(std::is_same_v<std::underlying_type_t<entt::entity>, std::uint32_t>);
		table.emplace(
		    entt::type_id<entt::entity>().hash(), Leaf{ValueKind::Entity, 4, 1, "jettison_entity"});
		return table;
	}();
	return leaves;
}

bool IsRegistered(const entt::meta_type &type)
{
	return static_cast<bool>(entt::resolve(type.info()));
}

// A pointer and a count read from a script, checked against its memory: count elements of size
// bytes each from the pointer. The arithmetic is 64-bit, so a count that would wrap does not.
std::span<const std::byte> Extent(
    std::span<const std::byte> memory, std::uint32_t pointer, std::uint32_t count, std::uint32_t size,
    std::string_view path)
{
	const std::uint64_t bytes = std::uint64_t{count} * size;
	if (pointer > memory.size() || bytes > memory.size() - pointer)
		throw BindingError{std::format(
		    "Script: {}: {} bytes at {} are past the end of the script's {} bytes of memory", path,
		    bytes, pointer, memory.size())};
	return memory.subspan(pointer, static_cast<std::size_t>(bytes));
}

std::uint32_t LoadU32(std::span<const std::byte> memory, std::uint32_t offset)
{
	std::uint32_t value = 0;
	std::memcpy(&value, memory.data() + offset, sizeof(value));
	return value;
}

void StoreU32(std::span<std::byte> memory, std::uint32_t offset, std::uint32_t value)
{
	std::memcpy(memory.data() + offset, &value, sizeof(value));
}

std::uint32_t Count32(std::size_t count, std::string_view path)
{
	if (count > std::numeric_limits<std::uint32_t>::max())
		throw BindingError{std::format("Script: {}: {} is too many for a 32-bit count", path, count)};
	return static_cast<std::uint32_t>(count);
}

// An enumerator's value as an integer of the enumeration's size, and back; by its bytes, so an
// enumeration of any underlying type reads the same way.
std::uint64_t EnumBits(const void *pValue, std::uint32_t size)
{
	std::uint64_t bits = 0;
	std::memcpy(&bits, pValue, size);
	return bits;
}

bool IsEnumerator(const entt::meta_type &type, std::uint64_t bits)
{
	for (const auto &[id, enumerator] : type.data())
	{
		const entt::meta_any value = enumerator.get({});
		if (EnumBits(ObjectOf(value), static_cast<std::uint32_t>(type.size_of())) == bits)
			return true;
	}
	return false;
}

// The engine's object for the field, through Reflect's address of it.
void *FieldIn(const FieldLayout &field, void *pObject)
{
	return Reflect::AddressOf(field.field, pObject);
}
const void *FieldIn(const FieldLayout &field, const void *pObject)
{
	return Reflect::AddressOf(field.field, const_cast<void *>(pObject));
}

// The sequence over an object of a vector type, through which its elements are read and added.
entt::meta_sequence_container Sequence(const entt::meta_type &type, const void *pObject)
{
	return type.from_void(pObject).as_sequence_container();
}
entt::meta_sequence_container Sequence(const entt::meta_type &type, void *pObject)
{
	return type.from_void(pObject).as_sequence_container();
}

// Out-of-line bytes: what a value's strings and arrays need after its struct.
std::uint64_t Extra(const ValueLayout &value, const void *pValue);

std::uint64_t ExtraOfStruct(const TypeLayout &layout, const void *pObject)
{
	std::uint64_t extra = 0;
	for (const FieldLayout &field : layout.fields)
		extra += Extra(field.value, FieldIn(field, pObject));
	return extra;
}

std::uint64_t Extra(const ValueLayout &value, const void *pValue)
{
	switch (value.kind)
	{
		case ValueKind::String: return static_cast<const std::string *>(pValue)->size();
		case ValueKind::Struct: return ExtraOfStruct(*value.pStruct, pValue);
		case ValueKind::Array:
		{
			entt::meta_sequence_container elements = Sequence(value.type, pValue);
			// The elements' block at its alignment, then what each element points at.
			std::uint64_t extra =
			    value.element->align - 1 + std::uint64_t{elements.size()} * value.element->size;
			for (const entt::meta_any element : elements)
				extra += Extra(*value.element, ObjectOf(element)) + value.element->align;
			return extra;
		}
		default: return 0;
	}
}

// Writes values into a block, its struct at the front and the rest bumped along after it.
class Writer
{
  public:
	Writer(std::span<std::byte> memory, std::uint32_t next, std::uint32_t end)
	    : m_memory{memory}, m_next{next}, m_end{end}
	{
	}

	void Struct(const TypeLayout &layout, const void *pObject, std::uint32_t at)
	{
		for (const FieldLayout &field : layout.fields)
			Value(field.value, FieldIn(field, pObject), at + field.offset, field.name);
	}

  private:
	std::uint32_t Take(std::uint64_t bytes, std::uint32_t align)
	{
		const std::uint32_t at = AlignUp(m_next, align);
		if (bytes > m_end - std::min(at, m_end))
			throw BindingError{"Script: a value outgrew the block sized for it"};
		m_next = at + static_cast<std::uint32_t>(bytes);
		return at;
	}

	void Value(const ValueLayout &value, const void *pValue, std::uint32_t at, std::string_view path)
	{
		std::byte *pAt = m_memory.data() + at;
		switch (value.kind)
		{
			case ValueKind::Bool:
			case ValueKind::Signed:
			case ValueKind::Unsigned:
			case ValueKind::Float:
			case ValueKind::Entity: std::memcpy(pAt, pValue, value.size); return;
			case ValueKind::Enum:
				if (!IsEnumerator(value.type, EnumBits(pValue, value.size)))
					throw BindingError{std::format(
					    "Script: {}: the value is not an enumerator registered for '{}'", path,
					    value.type.name())};
				std::memcpy(pAt, pValue, value.size);
				return;
			case ValueKind::String:
			{
				const auto &text = *static_cast<const std::string *>(pValue);
				const std::uint32_t size = Count32(text.size(), path);
				const std::uint32_t data = Take(size, 1);
				std::memcpy(m_memory.data() + data, text.data(), size);
				StoreU32(m_memory, at, size == 0 ? 0 : data);
				StoreU32(m_memory, at + 4, size);
				return;
			}
			case ValueKind::Struct: Struct(*value.pStruct, pValue, at); return;
			case ValueKind::Array:
			{
				entt::meta_sequence_container elements = Sequence(value.type, pValue);
				const std::uint32_t count = Count32(elements.size(), path);
				const std::uint32_t data =
				    Take(std::uint64_t{count} * value.element->size, value.element->align);
				std::uint32_t index = 0;
				for (const entt::meta_any element : elements)
				{
					Value(
					    *value.element, ObjectOf(element), data + index * value.element->size,
					    std::format("{}[{}]", path, index));
					++index;
				}
				StoreU32(m_memory, at, count == 0 ? 0 : data);
				StoreU32(m_memory, at + 4, count);
				return;
			}
		}
	}

	std::span<std::byte> m_memory;
	std::uint32_t m_next;
	std::uint32_t m_end;
};

class Reader
{
  public:
	explicit Reader(std::span<const std::byte> memory) : m_memory{memory}
	{
	}

	void Struct(const TypeLayout &layout, std::uint32_t at, void *pObject, std::string_view path)
	{
		for (const FieldLayout &field : layout.fields)
			Value(
			    field.value, at + field.offset, FieldIn(field, pObject),
			    std::format("{}.{}", path, field.name));
	}

  private:
	void Value(const ValueLayout &value, std::uint32_t at, void *pValue, const std::string &path)
	{
		const std::byte *pAt = m_memory.data() + at;
		switch (value.kind)
		{
			case ValueKind::Bool:
				if (std::to_integer<std::uint8_t>(*pAt) > 1)
					throw BindingError{
					    std::format("Script: {}: {} is not a bool", path, std::to_integer<int>(*pAt))};
				std::memcpy(pValue, pAt, value.size);
				return;
			case ValueKind::Signed:
			case ValueKind::Unsigned:
			case ValueKind::Float:
			case ValueKind::Entity: std::memcpy(pValue, pAt, value.size); return;
			case ValueKind::Enum:
			{
				const std::uint64_t bits = EnumBits(pAt, value.size);
				if (!IsEnumerator(value.type, bits))
					throw BindingError{std::format(
					    "Script: {}: {} is not an enumerator of '{}'", path, bits, value.type.name())};
				std::memcpy(pValue, pAt, value.size);
				return;
			}
			case ValueKind::String:
			{
				const std::span<const std::byte> bytes =
				    Extent(m_memory, LoadU32(m_memory, at), LoadU32(m_memory, at + 4), 1, path);
				static_cast<std::string *>(pValue)->assign(
				    reinterpret_cast<const char *>(bytes.data()), bytes.size());
				return;
			}
			case ValueKind::Struct: Struct(*value.pStruct, at, pValue, path); return;
			case ValueKind::Array:
			{
				const std::uint32_t data = LoadU32(m_memory, at);
				const std::uint32_t count = LoadU32(m_memory, at + 4);
				// Checked whole before anything is added, so a wild count allocates nothing.
				Extent(m_memory, data, count, value.element->size, path);
				entt::meta_sequence_container elements = Sequence(value.type, pValue);
				elements.clear();
				const entt::meta_type type = elements.value_type();
				for (std::uint32_t index = 0; index < count; ++index)
				{
					entt::meta_any element = type.construct();
					Value(
					    *value.element, data + index * value.element->size, ObjectOf(element),
					    std::format("{}[{}]", path, index));
					if (!elements.insert(elements.end(), std::move(element)))
						throw BindingError{
						    std::format("Script: {}: the element could not be added", path)};
				}
				return;
			}
		}
	}

	std::span<const std::byte> m_memory;
};
} // namespace

ValueLayout Bindings::ValueOf(const entt::meta_type &type, std::string_view path)
{
	ValueLayout value;
	value.type = type;
	if (const auto found = Leaves().find(type.info().hash()); found != Leaves().end())
	{
		const Leaf &leaf = found->second;
		value.kind = leaf.kind;
		value.width = leaf.width;
		value.count = leaf.count;
		value.size = std::uint32_t{leaf.width} * leaf.count;
		value.align = leaf.width;
		value.cName = leaf.cName;
		return value;
	}
	if (type.info().hash() == entt::type_id<std::string>().hash())
	{
		value.kind = ValueKind::String;
		value.size = kSpanSize;
		value.align = kSpanAlign;
		value.cName = "jettison_string";
		return value;
	}
	if (type.is_enum() && IsRegistered(type))
	{
		const auto size = static_cast<std::uint32_t>(type.size_of());
		value.kind = ValueKind::Enum;
		value.width = static_cast<std::uint8_t>(size);
		value.size = size;
		value.align = size;
		value.cName = CName(type.name());
		return value;
	}
	if (type.is_sequence_container() && !Reflect::FindOptional(type))
	{
		// The element type is the container's, which only an object of the type says.
		const entt::meta_any made = type.construct();
		const entt::meta_type elementType = made.as_sequence_container().value_type();
		auto pElement = std::make_shared<ValueLayout>(ValueOf(elementType, std::format("{}[]", path)));
		value.kind = ValueKind::Array;
		value.size = kSpanSize;
		value.align = kSpanAlign;
		value.cName = std::format("jettison_array_{}", pElement->cName);
		value.element = std::move(pElement);
		return value;
	}
	if (type.is_class() && IsRegistered(type))
	{
		const TypeLayout &nested = Layout(type);
		value.kind = ValueKind::Struct;
		value.size = nested.size;
		value.align = nested.align;
		value.cName = nested.cName;
		value.pStruct = &nested;
		return value;
	}
	throw BindingError{std::format(
	    "Script: {}: type '{}' is not one a script's memory holds", path, type.info().name())};
}

const TypeLayout &Bindings::Layout(const entt::meta_type &type)
{
	if (!type || !type.is_class() || !IsRegistered(type))
		throw BindingError{std::format(
		    "Script: '{}' is not a class registered with Reflect", type ? type.info().name() : "")};
	const entt::id_type id = type.info().hash();
	if (const auto found = m_layouts.find(id); found != m_layouts.end())
		return *found->second;
	if (std::ranges::find(m_making, id) != m_making.end())
		throw BindingError{std::format("Script: '{}' holds itself", type.name())};
	m_making.push_back(id);

	auto pLayout = std::make_unique<TypeLayout>();
	pLayout->type = type;
	pLayout->name = std::string{type.name()};
	pLayout->cName = CName(type.name());
	std::uint64_t offset = 0;
	for (const entt::meta_data &field : Reflect::Fields(type))
	{
		const std::string name{field.name()};
		const std::string path = std::format("{}.{}", type.name(), name);
		// A field the layout cannot hold is left out, and said to be.
		ValueLayout value;
		try
		{
			value = ValueOf(field.type(), path);
		}
		catch (const BindingError &)
		{
			if (std::ranges::find(m_making, field.type().info().hash()) != m_making.end())
			{
				m_making.pop_back();
				throw;
			}
			pLayout->omitted.push_back(std::format("{} ({})", name, field.type().info().name()));
			continue;
		}
		const std::uint32_t at = AlignUp(offset, value.align);
		pLayout->align = std::max(pLayout->align, value.align);
		offset = std::uint64_t{at} + value.size;
		pLayout->fields.push_back({field, name, at, std::move(value)});
	}
	pLayout->size = AlignUp(offset, pLayout->align);
	m_making.pop_back();
	return *m_layouts.emplace(id, std::move(pLayout)).first->second;
}

std::uint32_t Bindings::SizeOf(const TypeLayout &layout, const void *pObject) const
{
	const std::uint64_t total = std::uint64_t{layout.size} + ExtraOfStruct(layout, pObject);
	if (total > std::numeric_limits<std::uint32_t>::max())
		throw BindingError{std::format("Script: a '{}' is larger than a script's memory", layout.name)};
	return static_cast<std::uint32_t>(total);
}

void Bindings::Write(
    const TypeLayout &layout, const void *pObject, std::span<std::byte> memory,
    std::uint32_t offset) const
{
	const std::uint32_t size = SizeOf(layout, pObject);
	if (offset > memory.size() || size > memory.size() - offset)
		throw BindingError{std::format(
		    "Script: {} bytes for a '{}' at {} are past the end of memory", size, layout.name, offset)};
	std::memset(memory.data() + offset, 0, size);
	Writer writer{memory, offset + layout.size, offset + size};
	writer.Struct(layout, pObject, offset);
}

void Bindings::Read(
    const TypeLayout &layout, std::span<const std::byte> memory, std::uint32_t offset,
    void *pObject) const
{
	Extent(memory, offset, 1, layout.size, layout.name);
	Reader reader{memory};
	reader.Struct(layout, offset, pObject, layout.name);
}

std::string SendImport(const entt::meta_type &command)
{
	return std::format("send:{}", command.name());
}

namespace
{
// Declares what a header needs before a type: the structs its fields hold and the arrays of them,
// each once, depth first so each is declared before it is used.
class HeaderWriter
{
  public:
	void Type(const TypeLayout &layout)
	{
		if (!m_declared.insert(layout.cName).second)
			return;
		for (const FieldLayout &field : layout.fields)
			Needs(field.value);

		m_text += std::format("/* {} */\n", layout.name);
		m_text += std::format("typedef struct {}\n{{\n", layout.cName);
		for (const FieldLayout &field : layout.fields)
			m_text += std::format("\t{} {};\n", field.value.cName, field.name);
		if (layout.fields.empty())
			m_text += "\tuint8_t unused_;\n";
		for (const std::string &omitted : layout.omitted)
			m_text += std::format("\t/* {}: not seen by scripts */\n", omitted);
		m_text += std::format("}} {};\n", layout.cName);
		if (!layout.fields.empty())
		{
			m_text += std::format(
			    "_Static_assert(sizeof({}) == {}, \"{} is laid out as the host lays it out\");\n",
			    layout.cName, layout.size, layout.name);
			for (const FieldLayout &field : layout.fields)
				m_text += std::format(
				    "_Static_assert(offsetof({}, {}) == {}, \"{}.{}\");\n", layout.cName, field.name,
				    field.offset, layout.name, field.name);
		}
		m_text += "\n";
	}

	std::string Take()
	{
		return std::move(m_text);
	}

  private:
	void Needs(const ValueLayout &value)
	{
		if (value.kind == ValueKind::Struct)
			Type(*value.pStruct);
		else if (value.kind == ValueKind::Enum)
			Enum(value);
		else if (value.kind == ValueKind::Array)
		{
			Needs(*value.element);
			if (m_declared.insert(value.cName).second)
				m_text += std::format(
				    "typedef struct {0}\n{{\n\tconst {1} *data;\n\tuint32_t size;\n}} {0};\n\n",
				    value.cName, value.element->cName);
		}
	}

	void Enum(const ValueLayout &value)
	{
		if (!m_declared.insert(value.cName).second)
			return;
		m_text += std::format("/* {} */\n", value.type.name());
		m_text += std::format("typedef uint{}_t {};\n", value.size * 8, value.cName);
		for (const auto &[id, enumerator] : value.type.data())
		{
			const entt::meta_any held = enumerator.get({});
			m_text += std::format(
			    "#define {}_{} (({}){}u)\n", value.cName, enumerator.name(), value.cName,
			    EnumBits(ObjectOf(held), value.size));
		}
		m_text += "\n";
	}

	std::set<std::string, std::less<>> m_declared;
	std::string m_text;
};

constexpr std::string_view kPreamble =
    R"(/* The host API for Jettison's scripts, generated from the reflection tables of the game that wrote
 * it (Script::Header): the imports a script may be granted, and the game's types a script sees,
 * laid out as the host copies them in and out of a script's memory. Do not edit it; write it again
 * from the game, which the sample shell does with its console's "script_bindings <directory>".
 *
 * A script is a wasm32 module, compiled freestanding: clang --target=wasm32 -ffreestanding
 * -nostdlib, linked with wasm-ld --no-entry. Its handlers and inputs are exported by name
 * (JETTISON_EXPORT), and its definition (defs/scripts/<name>.yaml) names them, with what it is
 * granted. A module that imports what it is not granted is refused as it loads. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JETTISON_IMPORT(name) __attribute__((import_module("jettison"), import_name(name)))
#define JETTISON_EXPORT(name) __attribute__((export_name(name)))

typedef uint32_t jettison_entity;
#define JETTISON_NO_ENTITY ((jettison_entity)0xffffffffu)

/* A string's bytes, not terminated, and their count. An event's strings are the host's copy, valid
 * for the handler's call. */
typedef struct jettison_string
{
	const char *data;
	uint32_t size;
} jettison_string;
#define JETTISON_STRING(literal) ((jettison_string){(literal), sizeof(literal) - 1})

typedef struct jettison_vec2 { float x, y; } jettison_vec2;
typedef struct jettison_vec3 { float x, y, z; } jettison_vec3;
typedef struct jettison_vec4 { float x, y, z, w; } jettison_vec4;
typedef struct jettison_dvec2 { double x, y; } jettison_dvec2;
typedef struct jettison_dvec3 { double x, y, z; } jettison_dvec3;
typedef struct jettison_dvec4 { double x, y, z, w; } jettison_dvec4;
typedef struct jettison_quat { float x, y, z, w; } jettison_quat;
typedef struct jettison_dquat { double x, y, z, w; } jettison_dquat;

enum
{
	JETTISON_DEBUG = 0,
	JETTISON_INFO = 1,
	JETTISON_WARNING = 2,
	JETTISON_ERROR = 3
};

/* Granted by "log": a line in the game's log, under the script's name. */
JETTISON_IMPORT("log") void jettison_log(int32_t severity, const char *text, uint32_t size);

/* For a script that answers inputs, and only while it answers one: the option's input of that name,
 * as the game works it out. A name the game does not know traps. */
JETTISON_IMPORT("input") double jettison_input(const char *name, uint32_t size);

/* Granted by "store": keeps the bytes under the key, for this script alone and in the game's saves,
 * in whatever encoding the script chooses; no bytes take the key away. A store past the script's
 * keys or bytes traps, as does one while it answers an input. */
JETTISON_IMPORT("store")
void jettison_store(const char *key, uint32_t key_size, const void *data, uint32_t size);
/* Granted by "store": copies what is kept under the key into the buffer, as much as fits, and returns
 * how many bytes are kept, or -1 for none. */
JETTISON_IMPORT("load")
int32_t jettison_load(const char *key, uint32_t key_size, void *buffer, uint32_t capacity);

)";
} // namespace

namespace
{
// The layouts of the types, each once, in name order: what both the header and the module declare.
std::vector<const TypeLayout *> Sorted(Bindings &bindings, std::span<const entt::meta_type> types)
{
	std::vector<const TypeLayout *> layouts;
	for (const entt::meta_type &type : types)
		layouts.push_back(&bindings.Layout(type));
	std::ranges::sort(layouts, {}, &TypeLayout::name);
	const auto [first, last] = std::ranges::unique(layouts);
	layouts.erase(first, last);
	return layouts;
}

std::vector<entt::meta_type> Both(
    std::span<const entt::meta_type> types, std::span<const entt::meta_type> commands)
{
	std::vector<entt::meta_type> all{types.begin(), types.end()};
	all.insert(all.end(), commands.begin(), commands.end());
	return all;
}

// The Rust module's name for a value: the C header's for every type it declares itself, and Rust's
// own for a scalar, so a script in either language names the game's types the same way.
std::string RustName(const ValueLayout &value)
{
	if (value.count == 1)
		switch (value.kind)
		{
			case ValueKind::Bool: return "bool";
			case ValueKind::Signed: return std::format("i{}", value.width * 8);
			case ValueKind::Unsigned: return std::format("u{}", value.width * 8);
			case ValueKind::Float: return value.width == 4 ? "f32" : "f64";
			default: break;
		}
	return value.cName;
}

// A field named for a Rust keyword is written as a raw identifier.
std::string RustField(std::string_view name)
{
	static constexpr std::array<std::string_view, 39> kKeywords{
	    "as",   "async",  "await",  "break",  "const",  "continue", "crate", "dyn",
	    "else", "enum",   "extern", "false",  "fn",     "for",      "gen",   "if",
	    "impl", "in",     "let",    "loop",   "match",  "mod",      "move",  "mut",
	    "pub",  "ref",    "return", "static", "struct", "super",    "trait", "true",
	    "type", "unsafe", "use",    "where",  "while",  "yield",    "box"};
	return std::ranges::find(kKeywords, name) != kKeywords.end() ? std::format("r#{}", name)
	                                                             : std::string{name};
}

class RustWriter
{
  public:
	void Type(const TypeLayout &layout)
	{
		if (!m_declared.insert(layout.cName).second)
			return;
		for (const FieldLayout &field : layout.fields)
			Needs(field.value);

		m_text += std::format(
		    "/// {}\n#[repr(C)]\n#[derive(Clone, Copy)]\npub struct {} {{\n", layout.name,
		    layout.cName);
		for (const FieldLayout &field : layout.fields)
			m_text += std::format("    pub {}: {},\n", RustField(field.name), RustName(field.value));
		if (layout.fields.empty())
			m_text += "    pub unused_: u8,\n";
		for (const std::string &omitted : layout.omitted)
			m_text += std::format("    // {}: not seen by scripts\n", omitted);
		m_text += "}\n";
		if (!layout.fields.empty())
		{
			m_text += std::format(
			    "const _: () = assert!(size_of::<{}>() == {});\n", layout.cName, layout.size);
			for (const FieldLayout &field : layout.fields)
				m_text += std::format(
				    "const _: () = assert!(offset_of!({}, {}) == {});\n", layout.cName,
				    RustField(field.name), field.offset);
		}
		m_text += "\n";
	}

	std::string Take()
	{
		return std::move(m_text);
	}

  private:
	void Needs(const ValueLayout &value)
	{
		if (value.kind == ValueKind::Struct)
			Type(*value.pStruct);
		else if (value.kind == ValueKind::Enum)
			Enum(value);
		else if (value.kind == ValueKind::Array)
		{
			Needs(*value.element);
			if (m_declared.insert(value.cName).second)
				m_text += std::format(
				    "#[repr(C)]\n#[derive(Clone, Copy)]\npub struct {0} {{\n    pub data: *const {1},\n"
				    "    pub size: u32,\n}}\n"
				    "impl {0} {{\n    pub const fn new(items: &[{1}]) -> Self {{\n"
				    "        Self {{ data: items.as_ptr(), size: items.len() as u32 }}\n    }}\n"
				    "    pub fn as_slice(&self) -> &[{1}] {{\n"
				    "        if self.size == 0 {{ &[] }} else {{ unsafe {{ "
				    "slice::from_raw_parts(self.data, self.size as usize) }} }}\n"
				    "    }}\n}}\n\n",
				    value.cName, RustName(*value.element));
		}
	}

	void Enum(const ValueLayout &value)
	{
		if (!m_declared.insert(value.cName).second)
			return;
		m_text +=
		    std::format("/// {}\npub type {} = u{};\n", value.type.name(), value.cName, value.size * 8);
		for (const auto &[id, enumerator] : value.type.data())
		{
			const entt::meta_any held = enumerator.get({});
			m_text += std::format(
			    "pub const {0}_{1}: {0} = {2};\n", value.cName, enumerator.name(),
			    EnumBits(ObjectOf(held), value.size));
		}
		m_text += "\n";
	}

	std::set<std::string, std::less<>> m_declared;
	std::string m_text;
};

constexpr std::string_view kRustPreamble =
    R"(//! The host API for Jettison's scripts, generated from the reflection tables of the game that wrote
//! it (Script::RustModule): the imports a script may be granted, and the game's types a script
//! sees, laid out as the host copies them in and out of a script's memory, under the names the C
//! header gives them. Do not edit it; write it again from the game, which the sample shell does with
//! its console's "script_bindings <directory>".
//!
//! A script is a no_std crate of one file, built as a cdylib for wasm32-unknown-unknown with
//! panic=abort and linked by wasm-ld, which takes this module in with
//!     #[path = "jettison_script.rs"] mod jettison;
//! Its handlers and inputs are exported by name, #[unsafe(export_name = "...")] pub extern "C" fn,
//! and its definition (defs/scripts/<name>.yaml) names them, with what it is granted. A module that
//! imports what it is not granted is refused as it loads; a function here is imported only by a
//! script that calls it. The module brings the crate's panic handler, a trap.
#![allow(non_camel_case_types, non_upper_case_globals, non_snake_case, dead_code)]

use core::mem::{offset_of, size_of};
use core::slice;

pub type jettison_entity = u32;
pub const JETTISON_NO_ENTITY: jettison_entity = 0xffff_ffff;

/// A string's bytes, not terminated, and their count. An event's strings are the host's copy,
/// valid for the handler's call.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct jettison_string {
    pub data: *const u8,
    pub size: u32,
}
impl jettison_string {
    pub const fn new(text: &str) -> Self {
        Self { data: text.as_ptr(), size: text.len() as u32 }
    }
    pub fn as_bytes(&self) -> &[u8] {
        if self.size == 0 { &[] } else { unsafe { slice::from_raw_parts(self.data, self.size as usize) } }
    }
    /// The text, or nothing where it is not UTF-8.
    pub fn as_str(&self) -> &str {
        core::str::from_utf8(self.as_bytes()).unwrap_or("")
    }
}

#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_vec2 { pub x: f32, pub y: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_vec3 { pub x: f32, pub y: f32, pub z: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_vec4 { pub x: f32, pub y: f32, pub z: f32, pub w: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dvec2 { pub x: f64, pub y: f64 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dvec3 { pub x: f64, pub y: f64, pub z: f64 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dvec4 { pub x: f64, pub y: f64, pub z: f64, pub w: f64 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_quat { pub x: f32, pub y: f32, pub z: f32, pub w: f32 }
#[repr(C)] #[derive(Clone, Copy)] pub struct jettison_dquat { pub x: f64, pub y: f64, pub z: f64, pub w: f64 }

pub const JETTISON_DEBUG: i32 = 0;
pub const JETTISON_INFO: i32 = 1;
pub const JETTISON_WARNING: i32 = 2;
pub const JETTISON_ERROR: i32 = 3;

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

)";
} // namespace

std::string Header(
    Bindings &bindings, std::span<const entt::meta_type> types,
    std::span<const entt::meta_type> commands)
{
	HeaderWriter writer;
	for (const TypeLayout *pLayout : Sorted(bindings, Both(types, commands)))
		writer.Type(*pLayout);

	std::string text{kPreamble};
	text += writer.Take();
	for (const TypeLayout *pLayout : Sorted(bindings, commands))
		text += std::format(
		    "/* Granted by \"{0}\": the command, into the game's queue, applied at the start of the "
		    "next\n"
		    " * step and recorded with the player's. */\n"
		    "JETTISON_IMPORT(\"{0}\") void jettison_send_{1}(const {1} *command);\n\n",
		    SendImport(pLayout->type), pLayout->cName);
	return text;
}

std::string RustModule(
    Bindings &bindings, std::span<const entt::meta_type> types,
    std::span<const entt::meta_type> commands)
{
	RustWriter writer;
	for (const TypeLayout *pLayout : Sorted(bindings, Both(types, commands)))
		writer.Type(*pLayout);
	const std::vector<const TypeLayout *> sent = Sorted(bindings, commands);

	std::string text{kRustPreamble};
	text += writer.Take();
	text += "#[link(wasm_import_module = \"jettison\")]\nunsafe extern \"C\" {\n";
	text += "    /// Granted by \"log\": a line in the game's log, under the script's name.\n";
	text += "    #[link_name = \"log\"]\n    pub fn jettison_log(severity: i32, text: *const u8, size: "
	        "u32);\n";
	text += "    /// For a script that answers inputs, and only while it answers one: the option's "
	        "input of\n"
	        "    /// that name, as the game works it out. A name the game does not know traps.\n";
	text +=
	    "    #[link_name = \"input\"]\n    pub fn jettison_input(name: *const u8, size: u32) -> f64;\n";
	text += "    /// Granted by \"store\": keeps the bytes under the key, for this script alone and in "
	        "the\n"
	        "    /// game's saves; no bytes take the key away. A store past the script's keys or "
	        "bytes\n"
	        "    /// traps, as does one while it answers an input.\n"
	        "    #[link_name = \"store\"]\n"
	        "    pub fn jettison_store(key: *const u8, key_size: u32, data: *const u8, size: u32);\n"
	        "    /// Granted by \"store\": copies what is kept under the key into the buffer, as much "
	        "as\n"
	        "    /// fits, and returns how many bytes are kept, or -1 for none.\n"
	        "    #[link_name = \"load\"]\n"
	        "    pub fn jettison_load(key: *const u8, key_size: u32, buffer: *mut u8, capacity: u32) "
	        "-> i32;\n";
	for (const TypeLayout *pLayout : sent)
		text += std::format(
		    "    /// Granted by \"{0}\": the command, into the game's queue, applied at the start of "
		    "the\n"
		    "    /// next step and recorded with the player's.\n"
		    "    #[link_name = \"{0}\"]\n    pub fn jettison_send_{1}(command: *const {1});\n",
		    SendImport(pLayout->type), pLayout->cName);
	text += "}\n\n";
	text += "pub fn log(severity: i32, text: &str) {\n"
	        "    unsafe { jettison_log(severity, text.as_ptr(), text.len() as u32) }\n}\n\n";
	text += "pub fn input(name: &str) -> f64 {\n"
	        "    unsafe { jettison_input(name.as_ptr(), name.len() as u32) }\n}\n\n";
	text += "pub fn store(key: &str, data: &[u8]) {\n"
	        "    unsafe { jettison_store(key.as_ptr(), key.len() as u32, data.as_ptr(), data.len() as "
	        "u32) }\n}\n\n";
	text +=
	    "/// How many bytes are kept under the key, as much of them as fits copied into the buffer.\n"
	    "pub fn load(key: &str, buffer: &mut [u8]) -> Option<usize> {\n"
	    "    let kept = unsafe {\n"
	    "        jettison_load(key.as_ptr(), key.len() as u32, buffer.as_mut_ptr(), buffer.len() as "
	    "u32)\n"
	    "    };\n"
	    "    if kept < 0 { None } else { Some(kept as usize) }\n}\n";
	for (const TypeLayout *pLayout : sent)
		text += std::format(
		    "\npub fn send_{0}(command: &{0}) {{\n    unsafe {{ jettison_send_{0}(command) }}\n}}\n",
		    pLayout->cName);
	return text;
}
} // namespace Jettison::Script
