#include <jettison/sim/Reason.h>

#include <jettison/reflect/Reflect.h>

#include <format>
#include <stdexcept>
#include <string_view>

namespace Jettison::Sim
{
namespace
{
constexpr std::string_view kSuffix = ".Reason";

// "reason.dock." for "Dock.Reason".
std::string KeyPrefix(const entt::meta_type &reasons)
{
	const std::string_view name = reasons ? reasons.name() : std::string_view{};
	if (!reasons.is_enum() || name.size() <= kSuffix.size() || !name.ends_with(kSuffix) ||
	    name.substr(0, name.size() - kSuffix.size()).find('.') != std::string_view::npos)
		throw std::logic_error{std::format(
		    "Sim: '{}' is not a module's reasons, an enumeration registered as '<Module>.Reason'",
		    name.empty() ? std::string_view{"an unregistered type"} : name)};
	std::string prefix{"reason."};
	for (const char c : name.substr(0, name.size() - kSuffix.size()))
		prefix += c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
	prefix += '.';
	return prefix;
}
} // namespace

std::string ReasonKey(const entt::meta_any &enumerator)
{
	std::string key = KeyPrefix(enumerator.type());
	const std::string_view name = Reflect::EnumName(enumerator);
	if (name.empty())
		throw std::logic_error{std::format("Sim: an enumerator of '{}' is not registered", key)};
	key += name;
	return key;
}

std::vector<std::string> ReasonKeys(const entt::meta_type &reasons)
{
	const std::string prefix = KeyPrefix(reasons);
	std::vector<std::string> keys;
	for (const auto &[id, enumerator] : reasons.data())
		keys.push_back(prefix + std::string{enumerator.name()});
	return keys;
}

std::string Describe(const Reason &reason)
{
	std::string text = reason.key;
	text += '(';
	for (std::size_t index = 0; index < reason.arguments.size(); ++index)
	{
		const ReasonArgument &argument = reason.arguments[index];
		if (index > 0)
			text += ", ";
		switch (argument.kind)
		{
			case ReasonValue::Integer: text += std::to_string(argument.integer); break;
			case ReasonValue::Entity:
				text += std::format("entity {:#x}", entt::to_integral(argument.entity));
				break;
			case ReasonValue::Identifier:
				text += argument.definition.empty()
				            ? std::format("'{}'", argument.identifier)
				            : std::format("{} '{}'", argument.definition, argument.identifier);
				break;
		}
	}
	text += ')';
	return text;
}
} // namespace Jettison::Sim
