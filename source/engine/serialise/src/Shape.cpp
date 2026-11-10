#include "Shape.h"

#include <jettison/serialise/Archive.h>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstring>
#include <format>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace Jettison::Serialise::Detail
{
namespace
{
template <typename T> const void *View(const entt::meta_any &value)
{
	return value.try_cast<T>();
}

template <typename T> entt::meta_any MakeFromBytes(const void *components)
{
	T value{};
	std::memcpy(static_cast<void *>(&value), components, sizeof(T));
	return entt::meta_any{value};
}

entt::meta_any MakeString(const void *text)
{
	return entt::meta_any{*static_cast<const std::string *>(text)};
}

template <typename T> Kind ScalarKind()
{
	if constexpr (std::is_same_v<T, bool>)
		return Kind::Bool;
	else if constexpr (std::is_floating_point_v<T>)
		return Kind::Float;
	else if constexpr (std::is_signed_v<T>)
		return Kind::Signed;
	else
		return Kind::Unsigned;
}

template <typename T, typename Component, std::uint8_t Count> Shape Leaf()
{
	// Make copies the components into the value, so a GLM vector must be exactly its components.
	static_assert(sizeof(T) == sizeof(Component) * Count && std::is_trivially_copyable_v<T>);
	return {ScalarKind<Component>(), sizeof(Component), Count, &View<T>, &MakeFromBytes<T>};
}

template <typename T> void Add(std::unordered_map<entt::id_type, Shape> &shapes, Shape shape)
{
	shapes.emplace(entt::type_id<T>().hash(), shape);
}

// Lookups only: no outcome depends on this map's iteration order.
const std::unordered_map<entt::id_type, Shape> &LeafShapes()
{
	static const std::unordered_map<entt::id_type, Shape> shapes = []
	{
		std::unordered_map<entt::id_type, Shape> table;
		Add<bool>(table, Leaf<bool, bool, 1>());
		// Every standard integer type, by name rather than by width: std::int64_t is long here
		// and long long elsewhere, and a field of either must find its codec.
		Add<signed char>(table, Leaf<signed char, signed char, 1>());
		Add<short>(table, Leaf<short, short, 1>());
		Add<int>(table, Leaf<int, int, 1>());
		Add<long>(table, Leaf<long, long, 1>());
		Add<long long>(table, Leaf<long long, long long, 1>());
		Add<unsigned char>(table, Leaf<unsigned char, unsigned char, 1>());
		Add<unsigned short>(table, Leaf<unsigned short, unsigned short, 1>());
		Add<unsigned int>(table, Leaf<unsigned int, unsigned int, 1>());
		Add<unsigned long>(table, Leaf<unsigned long, unsigned long, 1>());
		Add<unsigned long long>(table, Leaf<unsigned long long, unsigned long long, 1>());
		Add<float>(table, Leaf<float, float, 1>());
		Add<double>(table, Leaf<double, double, 1>());
		Add<glm::vec2>(table, Leaf<glm::vec2, float, 2>());
		Add<glm::vec3>(table, Leaf<glm::vec3, float, 3>());
		Add<glm::vec4>(table, Leaf<glm::vec4, float, 4>());
		Add<glm::dvec2>(table, Leaf<glm::dvec2, double, 2>());
		Add<glm::dvec3>(table, Leaf<glm::dvec3, double, 3>());
		Add<glm::dvec4>(table, Leaf<glm::dvec4, double, 4>());
		// A quaternion is its components in GLM's storage order, x, y, z then w: the order a
		// save holds them in is the order the type holds them in, and GLM_FORCE_QUAT_DATA_WXYZ
		// would change both.
		Add<glm::quat>(table, Leaf<glm::quat, float, 4>());
		Add<glm::dquat>(table, Leaf<glm::dquat, double, 4>());
		// An entity is its 32-bit identifier, index and version together: what a component that
		// refers to another entity holds, restored as it was by a world's loader.
		static_assert(std::is_same_v<std::underlying_type_t<entt::entity>, std::uint32_t>);
		Add<entt::entity>(table, Leaf<entt::entity, std::uint32_t, 1>());
		Add<std::string>(table, Shape{Kind::String, 0, 1, &View<std::string>, &MakeString});
		return table;
	}();
	return shapes;
}

bool IsRegistered(const entt::meta_type &type)
{
	// resolve(type_info) finds registered types only, unlike the unnamed node EnTT keeps for
	// every type it has seen.
	return static_cast<bool>(entt::resolve(type.info()));
}
} // namespace

Shape ShapeOf(const entt::meta_type &type, std::string_view path)
{
	if (!type)
		throw UnsupportedTypeError{std::format("Serialise: {}: there is no value", path)};
	const auto &leaves = LeafShapes();
	if (const auto found = leaves.find(type.info().hash()); found != leaves.end())
		return found->second;
	if (type.is_enum())
	{
		if (!IsRegistered(type))
			throw UnsupportedTypeError{std::format(
			    "Serialise: {}: enumeration '{}' is not registered with Reflect", path,
			    type.info().name())};
		return {Kind::Enum};
	}
	if (type.is_sequence_container())
		return {Kind::Sequence};
	if (type.is_class() && IsRegistered(type))
		return {Kind::Class};
	throw UnsupportedTypeError{std::format(
	    "Serialise: {}: type '{}' is neither registered with Reflect nor one an archive writes", path,
	    type.info().name())};
}

Path::Scope Path::Field(std::string_view name)
{
	const std::size_t length = m_text.size();
	m_text += '.';
	m_text += name;
	return Scope{*this, length};
}

Path::Scope Path::Element(std::size_t index)
{
	const std::size_t length = m_text.size();
	m_text += std::format("[{}]", index);
	return Scope{*this, length};
}

std::string_view RootName(const entt::meta_type &type)
{
	if (!type || !type.is_class() || !IsRegistered(type))
		throw UnsupportedTypeError{std::format(
		    "Serialise: an archive's root is a class registered with Reflect, not '{}'",
		    type ? type.info().name() : std::string_view{"nothing"})};
	return type.name();
}
} // namespace Jettison::Serialise::Detail
