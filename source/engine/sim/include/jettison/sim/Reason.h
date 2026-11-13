#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Jettison::Sim
{
// Why the step refused or gave something up, as an event carries it (docs/roadmap.md, "Decided": an
// event says why by a reason and its arguments): a key the string tables word, and the values the
// text is formatted with, so the player reads it in their language and the step makes no English.
// The step's log lines stay English, for developers.
//
// An argument is an integer, which keeps currency whole, an entity, which presentation words by its
// name in a snapshot, or an identifier from the game's data, a definition's or a body's. They are
// positional, {0} and {1}, as the tables' arguments are, so a translation is held to them by the
// check that holds it to its base text.

enum class ReasonValue : std::uint8_t
{
	Integer,
	Entity,
	Identifier
};

// Registered with Reflect, every field Saved, by RegisterTypes.
struct ReasonArgument
{
	ReasonValue kind{ReasonValue::Integer};
	std::int64_t integer{0};
	entt::entity entity{entt::null};
	std::string identifier;
	// What the identifier names: the type Reflect registered its definition under ("Shell.Good"),
	// or empty for a name the data gives that reads as itself, a body's, a system's or a jump
	// point's.
	std::string definition;

	static ReasonArgument Integer(std::int64_t value)
	{
		return {ReasonValue::Integer, value, entt::null, {}, {}};
	}
	static ReasonArgument Entity(entt::entity value)
	{
		return {ReasonValue::Entity, 0, value, {}, {}};
	}
	static ReasonArgument Identifier(std::string value, std::string definition = {})
	{
		return {ReasonValue::Identifier, 0, entt::null, std::move(value), std::move(definition)};
	}

	bool operator==(const ReasonArgument &) const = default;
};

// Registered with Reflect, every field Saved, by RegisterTypes.
struct Reason
{
	// "reason.dock.no_pad_fits"; a script's is "reason.script.<script>.<name>".
	std::string key;
	std::vector<ReasonArgument> arguments;

	bool operator==(const Reason &) const = default;
};

// The key of an enumerator of a module's reasons: an enumeration registered with Reflect as
// "<Module>.Reason", each enumerator under the key's last part, in snake case, so
// Dock::Reason::NoPadFits, registered as "no_pad_fits" in "Dock.Reason", is
// "reason.dock.no_pad_fits", and two modules cannot make one key. Throws std::logic_error for an
// enumeration registered otherwise, or not at all, and for an enumerator not registered.
std::string ReasonKey(const entt::meta_any &enumerator);

template <typename E> Reason MakeReason(E enumerator, std::vector<ReasonArgument> arguments = {})
{
	static_assert(std::is_enum_v<E>, "a reason is an enumerator of a module's reasons");
	return {ReasonKey(entt::meta_any{enumerator}), std::move(arguments)};
}

// The keys of every enumerator of a module's reasons, in registration order: what the tables have
// to word.
std::vector<std::string> ReasonKeys(const entt::meta_type &reasons);

// The reason as a developer reads it, in a log line or a test's failure, and in no language:
// "reason.shell.short_of_stock(entity 0x2a, 12)".
std::string Describe(const Reason &reason);
} // namespace Jettison::Sim
