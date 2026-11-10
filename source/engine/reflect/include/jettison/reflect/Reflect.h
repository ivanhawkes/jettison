#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <string_view>

namespace Jettison::Reflect
{
// Attributes a registered type or field can carry, read by serialisation (Saved), the editor
// inspector (Editable), the definition registry (Moddable) and the console (Console). They are
// EnTT meta properties under the hood; this is the vocabulary so every reader agrees on it.
enum class Attribute : std::uint32_t
{
	Saved = 1u << 0,
	Editable = 1u << 1,
	Moddable = 1u << 2,
	Console = 1u << 3,
};

// Registration helpers over entt::meta. A type is registered once, by name, with its fields and
// their attributes; the archive, the inspector and the console then walk the meta data rather
// than each knowing the type. Filled in phase 3.
template <typename T> class TypeBuilder
{
  public:
	explicit TypeBuilder(std::string_view name);

	template <auto Member> TypeBuilder &Field(std::string_view name, std::uint32_t attributes = 0);
};

template <typename T> TypeBuilder<T> RegisterType(std::string_view name);

// The registered type for a name, or an empty meta_type.
entt::meta_type Resolve(std::string_view name);
} // namespace Jettison::Reflect
