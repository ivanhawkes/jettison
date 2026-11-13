#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Jettison::Reflect
{
// Attributes a registered type or field can carry, read by serialisation (Saved), the editor
// inspector (Editable), the definition registry (Moddable), the console (Console) and the script
// host (Script: a type scripts see, every field of it, as an event a handler is given or a command
// a script may be granted to send; docs/architecture.md, "Scripts"). They are
// EnTT meta traits under the hood, which leaves the user 15 bits of a 32-bit word: the
// underlying type is 16 bits so a sixteenth attribute is a compile error, not a wrapped value.
enum class Attribute : std::uint16_t
{
	None = 0,
	Saved = 1u << 0,
	Editable = 1u << 1,
	Moddable = 1u << 2,
	Console = 1u << 3,
	Script = 1u << 4,
};

constexpr Attribute operator|(Attribute left, Attribute right)
{
	return static_cast<Attribute>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
}

constexpr Attribute operator&(Attribute left, Attribute right)
{
	return static_cast<Attribute>(static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
}

// Whether every attribute in wanted is in set. HasAll(set, Attribute::None) is true.
constexpr bool HasAll(Attribute set, Attribute wanted)
{
	return (set & wanted) == wanted;
}

// A name in the reflection tables. EnTT keeps the pointer it is given, not a copy, so the text
// must outlive every lookup. The constructor is consteval, which makes that a compile error
// rather than a dangling name: only a constant expression constructs one, and a pointer that is
// a constant expression points at static storage, a string literal or a constexpr array.
// The ID is EnTT's 32-bit FNV-1a of the text, which lives only as long as the program runs: a
// file names a type or a field by its text, never by this ID.
class Name
{
  public:
	consteval Name(const char *text)
	    : m_text{text}, m_id{entt::hashed_string::value(text, std::char_traits<char>::length(text))}
	{
	}

	constexpr const char *Text() const
	{
		return m_text;
	}
	constexpr entt::id_type Id() const
	{
		return m_id;
	}

  private:
	const char *m_text;
	entt::id_type m_id;
};

// What registration throws: a type registered twice, a name another type already has, a field
// name used twice in one type. Each is a defect in start-up code, named in the message.
class RegistrationError : public std::logic_error
{
  public:
	explicit RegistrationError(const std::string &message) : std::logic_error{message}
	{
	}
};

// What an archive needs to handle a std::optional<T> field: whether it holds a value, a copy of the
// value, and an optional made from one. A field that is optional is one a file may leave out and the
// value still means something, as opposed to a field whose default is what leaving it out means: a
// scene entity's light, which most entities have none of. The value type is any type an archive
// writes, registered with Reflect if it is a class.
struct OptionalTraits
{
	entt::meta_type (*valueType)();
	bool (*hasValue)(const entt::meta_any &optional);
	entt::meta_any (*value)(const entt::meta_any &optional);
	entt::meta_any (*make)(entt::meta_any &value);
};

// The traits for a std::optional type some registered field has, or null for any other type.
const OptionalTraits *FindOptional(const entt::meta_type &type);

// Where a field is in an object of its type: what a reader needs to walk an object in place rather
// than copy each field out and back, as the archives do. The UI's data models read the presentation's
// copy of a snapshot through it, every frame (ui/DataModel.h).
struct FieldAddress
{
	void *(*address)(void *object);
};

// The field's address in the object, which is of the field's type's owner; null for an object that
// is null, or a field not registered through TypeBuilder::Field.
void *AddressOf(const entt::meta_data &field, void *object);

namespace Detail
{
template <typename T, auto Member> void *MemberAddress(void *object)
{
	return &(static_cast<T *>(object)->*Member);
}

// Throws RegistrationError unless name is free for a type and the type is not registered yet.
void CheckTypeName(Name name, const entt::type_info &info);
// Throws RegistrationError if type already has a field or an enumerator by that name.
void CheckFieldName(const entt::meta_type &type, Name name);
// Records an optional type's traits the first time a field of that type is registered.
void AddOptional(const entt::type_info &info, const OptionalTraits &traits);

template <typename T> struct IsOptional : std::false_type
{
};
template <typename T> struct IsOptional<std::optional<T>> : std::true_type
{
};

template <typename T> struct OptionalOf
{
	using Value = typename T::value_type;

	static entt::meta_type ValueType()
	{
		return entt::resolve<Value>();
	}
	static bool HasValue(const entt::meta_any &optional)
	{
		const T *pOptional = optional.try_cast<T>();
		return pOptional != nullptr && pOptional->has_value();
	}
	static entt::meta_any GetValue(const entt::meta_any &optional)
	{
		return entt::meta_any{**optional.try_cast<T>()};
	}
	static entt::meta_any Make(entt::meta_any &value)
	{
		return entt::meta_any{T{value.cast<Value>()}};
	}

	static constexpr OptionalTraits kTraits{&ValueType, &HasValue, &GetValue, &Make};
};
} // namespace Detail

// Registers T by name, with the fields its builder adds and the attributes given here. A type is
// registered once: a second registration of T, or another type under the same name, throws.
// Registration happens at start-up, before any reader walks the tables, and is not thread-safe.
//
// Fields are member object pointers of T. They are listed in registration order, which is the
// order an archive writes them in. Bases are not reflected: components and definitions are plain
// structs (docs/coding-standards.md, "Components and state").
template <typename T> class TypeBuilder
{
	static_assert(std::is_class_v<T>, "TypeBuilder registers a class; enums go through EnumBuilder");

  public:
	explicit TypeBuilder(Name name, Attribute attributes = Attribute::None) : m_factory{Start(name)}
	{
		m_factory.traits(attributes);
	}

	template <auto Member> TypeBuilder &Field(Name name, Attribute attributes = Attribute::None)
	{
		static_assert(std::is_member_object_pointer_v<decltype(Member)>, "A field is a data member");
		static_assert(std::is_invocable_v<decltype(Member), T &>, "The field is not a member of T");
		Detail::CheckFieldName(entt::resolve<T>(), name);
		using FieldType = std::remove_cvref_t<std::invoke_result_t<decltype(Member), T &>>;
		if constexpr (Detail::IsOptional<FieldType>::value)
			Detail::AddOptional(entt::type_id<FieldType>(), Detail::OptionalOf<FieldType>::kTraits);
		m_factory.template data<Member>(name.Id(), name.Text())
		    .traits(attributes)
		    .template custom<FieldAddress>(FieldAddress{&Detail::MemberAddress<T, Member>});
		return *this;
	}

  private:
	// Constructing a meta_factory adds T to the tables, so the checks run before it exists.
	static entt::meta_factory<T> Start(Name name)
	{
		Detail::CheckTypeName(name, entt::type_id<T>());
		return entt::meta_factory<T>{}.type(name.Id(), name.Text());
	}

	entt::meta_factory<T> m_factory;
};

// Registers an enumeration by name, with each enumerator registered by name, so a file stores
// "Docked" rather than 3 and reordering the enumeration does not change what a file means.
template <typename E> class EnumBuilder
{
	static_assert(std::is_enum_v<E>, "EnumBuilder registers an enumeration");

  public:
	explicit EnumBuilder(Name name) : m_factory{Start(name)}
	{
	}

	template <E Enumerator> EnumBuilder &Value(Name name)
	{
		Detail::CheckFieldName(entt::resolve<E>(), name);
		m_factory.template data<Enumerator>(name.Id(), name.Text());
		return *this;
	}

  private:
	static entt::meta_factory<E> Start(Name name)
	{
		Detail::CheckTypeName(name, entt::type_id<E>());
		return entt::meta_factory<E>{}.type(name.Id(), name.Text());
	}

	entt::meta_factory<E> m_factory;
};

template <typename T> TypeBuilder<T> RegisterType(Name name, Attribute attributes = Attribute::None)
{
	return TypeBuilder<T>{name, attributes};
}

template <typename E> EnumBuilder<E> RegisterEnum(Name name)
{
	return EnumBuilder<E>{name};
}

// The registered type for a name, or an empty meta_type. The name is compared as text as well as
// by hash, so two names whose 32-bit hashes collide do not resolve to each other.
entt::meta_type Resolve(std::string_view name);

// The attributes a registered type or field was given.
Attribute AttributesOf(const entt::meta_type &type);
Attribute AttributesOf(const entt::meta_data &field);

// The fields of a registered type that carry every attribute in wanted, in registration order.
// Fields(type) is all of them.
std::vector<entt::meta_data> Fields(const entt::meta_type &type, Attribute wanted = Attribute::None);

// The field of that name, or an empty meta_data. Compared as text, as Resolve is.
entt::meta_data FindField(const entt::meta_type &type, std::string_view name);

// The enumerator's registered name, or an empty view for a value that was not registered (or a
// meta_any that does not hold a registered enumeration).
std::string_view EnumName(const entt::meta_any &value);

// The enumerator registered under that name in an enumeration's type, or an empty meta_any.
entt::meta_any EnumValue(const entt::meta_type &type, std::string_view name);
} // namespace Jettison::Reflect
