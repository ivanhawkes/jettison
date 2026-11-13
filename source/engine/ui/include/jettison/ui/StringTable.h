#pragma once

#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Ui
{
// The text a player reads, in their language (docs/architecture.md, "Localisation"): string tables
// read through the VFS, a key to a text, which documents name as [[market.buy]] and the game's code
// formats with Format and Plural. Developer text - the log, the console, the dev tools - stays in
// English and does not come through here.
//
// A language's tables are every file under strings/<language>/, YAML, a version and a mapping:
//
//   version: 1
//   market:
//     buy: Buy
//     crossing:
//       one: "; {0} crate crossing the deck"
//       other: "; {0} crates crossing the deck"
//
// nested mappings joined into keys by dots: "market.buy", "market.crossing.one". The same file in
// several mounts is merged key by key, the bottom mount's first, so a mod changes the strings it
// names and keeps the rest; a mod's new file adds strings, and a mod's new directory a language. A
// key two files of one language both define is refused, as a mistake that would otherwise depend on
// the files' order.
//
// A language is the base, English ("en"), which every key has; a language such as "pt-BR" is read
// over its primary language, "pt", which is read over the base, so a key a translation lacks reads
// the base's text. A key no table has reads as the key itself, and counts as a problem, which the
// smoke test holds at none.
//
// A text formats its arguments as std::format does, by position, {0} and {1:>8}, so a translation
// can put them in its own order; a document's text may hold RmlUi's data expressions, {{screen.used}},
// and markup, both of which RmlUi reads after the text replaces the key. A translation that names an
// argument or a data expression its base text does not is refused as it is read, and the base's
// text stands, so a translation cannot make a format fail or a document read what the screen does
// not bind.
class StringTable
{
  public:
	static constexpr std::string_view kBaseLanguage = "en";
	static constexpr std::string_view kDirectory = "strings";

	// Reads the language's tables over the base's, replacing what the table held. Throws
	// std::runtime_error naming the file, its mount and the line for a file that is not a table,
	// for a language that is not a language tag ("de", "pt-BR"), and when the base has no tables.
	// A language with no tables of its own reads as the base, and is logged.
	void Load(const Vfs::FileSystem &files, std::string_view language);

	const std::string &Language() const
	{
		return m_language;
	}
	// The languages with tables under strings/, sorted.
	static std::vector<std::string> Languages(const Vfs::FileSystem &files);

	// The key's text, or nothing, without counting it missing: for text that falls back to the
	// game's data, as a good's name falls back to its definition's.
	std::optional<std::string_view> Find(std::string_view key) const;
	// The key's text, or the key, counted and logged once, when no table has it.
	std::string_view Text(std::string_view key) const;

	// The key's text with the arguments formatted into it. A text whose arguments do not suit the
	// call, which only the base's own can be, is logged and read unformatted.
	template <typename... Args> std::string Format(std::string_view key, const Args &...args) const
	{
		return FormatText(key, Text(key), std::make_format_args(args...));
	}
	// The form of the key for the count, key.one or key.few, as the language's plural rule has it for
	// a whole number (the CLDR's), or key.other when the table has no such form; the count is {0}
	// and the arguments follow it.
	template <typename... Args>
	std::string Plural(std::string_view key, std::int64_t count, const Args &...args) const
	{
		return FormatText(key, PluralText(key, count), std::make_format_args(count, args...));
	}

	// Format and Plural for arguments known only as the game runs, each already words, as a
	// reason's are: at most kMaxArguments, past which the rest are left out and the call counted as
	// a problem. Plural's count chooses the form and is not itself an argument.
	static constexpr std::size_t kMaxArguments = 8;
	std::string FormatArguments(std::string_view key, std::span<const std::string> arguments) const;
	std::string PluralArguments(
	    std::string_view key, std::int64_t count, std::span<const std::string> arguments) const;

	// A whole number as the language writes it, its thousands grouped by the table's
	// numbers.thousands: 1,250,000 in English, 1.250.000 in German. What a document's
	// {{value | thousands}} shows.
	std::string Number(std::int64_t value) const;

	// Each [[key]] in the text replaced with the key's text, as RmlUi asks of a document's text;
	// returns how many were.
	std::string Translate(std::string_view text, int &replaced) const;

	std::size_t Count() const
	{
		return m_texts.size();
	}
	// Keys asked for that no table has, and translations refused.
	std::size_t Problems() const
	{
		return m_problems;
	}

	// The CLDR plural category of a whole number in the language: "zero", "one", "two", "few", "many"
	// or "other".
	static std::string_view PluralCategory(std::string_view language, std::int64_t count);

  private:
	std::string FormatText(
	    std::string_view key, std::string_view text, std::format_args arguments) const;
	std::string_view PluralText(std::string_view key, std::int64_t count) const;
	// The text as it is, for a call it does not format, logged and counted once.
	std::string Unformatted(
	    std::string_view key, std::string_view text, const std::format_error &error) const;
	std::string FormatSpan(
	    std::string_view key, std::string_view text, std::span<const std::string> arguments) const;

	std::string m_language{kBaseLanguage};
	std::map<std::string, std::string, std::less<>> m_texts;
	mutable std::set<std::string, std::less<>> m_missing;
	mutable std::size_t m_problems{0};
};
} // namespace Jettison::Ui
