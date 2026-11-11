#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <jettison/reflect/Reflect.h>

// What the two archives share: how a type is written, decided once from its meta type, and the
// path that names a value in an error.
namespace Jettison::Serialise::Detail
{
enum class Kind : std::uint8_t
{
	Bool,
	Signed,
	Unsigned,
	Float,
	String,
	Enum,
	Class,
	Sequence,
	// A std::optional field: written only when it holds a value, and read as holding one when the
	// data names it.
	Optional,
};

// Bool, Signed, Unsigned and Float are leaves of one or more components of one width, stored
// contiguously: a scalar is one component, a GLM vector several. View and Make move a leaf
// between a meta_any and those component bytes; a String is viewed and made as std::string.
struct Shape
{
	Kind kind{Kind::Class};
	std::uint8_t width{0};
	std::uint8_t count{1};
	const void *(*view)(const entt::meta_any &){nullptr};
	entt::meta_any (*make)(const void *){nullptr};
	const Reflect::OptionalTraits *optional{nullptr};
};

// Whether a field's value is an optional holding nothing, which an archive leaves out.
bool IsEmptyOptional(const entt::meta_any &value);

// The shape of a type, or UnsupportedTypeError naming the path. The type of a Class shape is
// registered with Reflect.
Shape ShapeOf(const entt::meta_type &type, std::string_view path);

// The widest leaf: four components of eight bytes, a glm::dvec4.
inline constexpr std::size_t kMaxLeafBytes = 32;

// A value's path for an error message, "Cargo.holds[2].mass", built as the archive descends.
class Path
{
  public:
	explicit Path(std::string_view root) : m_text{root}
	{
	}

	std::string_view Text() const
	{
		return m_text;
	}

	// Appends ".name" or "[index]" for the life of the returned scope.
	class Scope
	{
	  public:
		Scope(Path &path, std::size_t length) : m_path{path}, m_length{length}
		{
		}
		~Scope()
		{
			m_path.m_text.resize(m_length);
		}
		Scope(const Scope &) = delete;
		Scope &operator=(const Scope &) = delete;

	  private:
		Path &m_path;
		std::size_t m_length;
	};

	[[nodiscard]] Scope Field(std::string_view name);
	[[nodiscard]] Scope Element(std::size_t index);

  private:
	std::string m_text;
};

// Throws UnsupportedTypeError unless type is a class registered with Reflect, which is what an
// archive's root must be. The name is what errors and the binary header use.
std::string_view RootName(const entt::meta_type &type);
} // namespace Jettison::Serialise::Detail
