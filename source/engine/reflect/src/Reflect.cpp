#include <jettison/reflect/Reflect.h>

#include <format>

namespace Jettison::Reflect
{
namespace
{
entt::id_type IdOf(std::string_view text)
{
	return entt::hashed_string::value(text.data(), text.size());
}
} // namespace

namespace Detail
{
void CheckTypeName(Name name, const entt::type_info &info)
{
	// The overload taking a type_info finds registered types only; resolve<T>() would also return
	// the unnamed node EnTT keeps for every type it has seen.
	if (const entt::meta_type existing = entt::resolve(info))
		throw RegistrationError{std::format(
		    "Reflect: type '{}' is already registered as '{}'", info.name(), existing.name())};

	// EnTT's own check for a duplicate alias is an assertion, absent in release; this one is not.
	// The lookup by ID also finds a name whose hash collides with a different name.
	if (const entt::meta_type existing = entt::resolve(name.Id()))
	{
		if (existing.name() == name.Text())
			throw RegistrationError{std::format(
			    "Reflect: name '{}' is already registered for type '{}'", name.Text(),
			    existing.info().name())};
		throw RegistrationError{std::format(
		    "Reflect: name '{}' hashes to the ID of '{}'; rename one of them", name.Text(),
		    existing.name())};
	}
}

void CheckFieldName(const entt::meta_type &type, Name name)
{
	// EnTT replaces a field registered twice under one ID without a word, so a second field of
	// the same name, or of a colliding one, would silently take the first one's place.
	if (const entt::meta_data existing = type.data(name.Id(), false))
	{
		if (existing.name() == name.Text())
			throw RegistrationError{
			    std::format("Reflect: '{}' already has a field '{}'", type.name(), name.Text())};
		throw RegistrationError{std::format(
		    "Reflect: field '{}' of '{}' hashes to the ID of '{}'; rename one of them", name.Text(),
		    type.name(), existing.name())};
	}
}
} // namespace Detail

entt::meta_type Resolve(std::string_view name)
{
	const entt::meta_type type = entt::resolve(IdOf(name));
	return (type && type.name() == name) ? type : entt::meta_type{};
}

Attribute AttributesOf(const entt::meta_type &type)
{
	return type.traits<Attribute>();
}

Attribute AttributesOf(const entt::meta_data &field)
{
	return field.traits<Attribute>();
}

std::vector<entt::meta_data> Fields(const entt::meta_type &type, Attribute wanted)
{
	std::vector<entt::meta_data> fields;
	for (const auto &[id, field] : type.data())
	{
		if (HasAll(AttributesOf(field), wanted))
			fields.push_back(field);
	}
	return fields;
}

entt::meta_data FindField(const entt::meta_type &type, std::string_view name)
{
	const entt::meta_data field = type.data(IdOf(name), false);
	return (field && field.name() == name) ? field : entt::meta_data{};
}

std::string_view EnumName(const entt::meta_any &value)
{
	const entt::meta_type type = value.type();
	if (!type || !type.is_enum())
		return {};
	for (const auto &[id, enumerator] : type.data())
	{
		if (enumerator.get({}) == value)
			return enumerator.name();
	}
	return {};
}

entt::meta_any EnumValue(const entt::meta_type &type, std::string_view name)
{
	if (!type.is_enum())
		return {};
	const entt::meta_data enumerator = FindField(type, name);
	return enumerator ? enumerator.get({}) : entt::meta_any{};
}
} // namespace Jettison::Reflect
