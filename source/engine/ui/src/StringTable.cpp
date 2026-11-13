#include <jettison/ui/StringTable.h>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
#include <utility>

#include <jettison/core/Log.h>
#include <jettison/vfs/FileSystem.h>

namespace Jettison::Ui
{
namespace
{
constexpr Core::Category kStrings{"strings"};
constexpr int kFormatVersion = 1;
constexpr std::string_view kExtension = ".yaml";
constexpr std::array<std::string_view, 6> kCategories{"zero", "one", "two", "few", "many", "other"};

// A text and where it was read, for the messages that name it.
struct Entry
{
	std::string text;
	std::string file;
};
using Entries = std::map<std::string, Entry, std::less<>>;

// What a text refers to: the arguments by position and the data expressions it names.
struct References
{
	std::set<unsigned> arguments;
	std::set<std::string, std::less<>> expressions;
};

std::string_view Trim(std::string_view text)
{
	while (!text.empty() && text.front() == ' ')
		text.remove_prefix(1);
	while (!text.empty() && text.back() == ' ')
		text.remove_suffix(1);
	return text;
}

// The braces of a text as std::format and RmlUi read them: {0} or {0:spec} an argument, {{ and }} a
// data expression between them, and }} alone a brace. Nothing, for a brace that is none of these.
std::optional<References> FindReferences(std::string_view text)
{
	References references;
	for (std::size_t at = 0; at < text.size(); ++at)
	{
		if (text[at] == '{')
		{
			if (at + 1 < text.size() && text[at + 1] == '{')
			{
				const std::size_t end = text.find("}}", at + 2);
				if (end == std::string_view::npos)
					return std::nullopt;
				references.expressions.emplace(Trim(text.substr(at + 2, end - at - 2)));
				at = end + 1;
				continue;
			}
			std::size_t cursor = at + 1;
			unsigned index = 0;
			while (cursor < text.size() && text[cursor] >= '0' && text[cursor] <= '9')
				index = index * 10 + static_cast<unsigned>(text[cursor++] - '0');
			if (cursor == at + 1)
				return std::nullopt;
			const std::size_t end = text.find('}', cursor);
			if (end == std::string_view::npos || (text[cursor] != '}' && text[cursor] != ':'))
				return std::nullopt;
			references.arguments.insert(index);
			at = end;
		}
		else if (text[at] == '}')
		{
			if (at + 1 >= text.size() || text[at + 1] != '}')
				return std::nullopt;
			++at;
		}
	}
	return references;
}

bool IsLanguageTag(std::string_view tag)
{
	// A primary language of two or three letters, then subtags of letters and digits: "de", "pt-BR".
	std::size_t at = 0;
	while (at < tag.size() && tag[at] >= 'a' && tag[at] <= 'z')
		++at;
	if (at < 2 || at > 3)
		return false;
	while (at < tag.size())
	{
		if (tag[at++] != '-')
			return false;
		const std::size_t start = at;
		while (at < tag.size() && std::isalnum(static_cast<unsigned char>(tag[at])) != 0)
			++at;
		if (at - start < 2 || at - start > 8)
			return false;
	}
	return true;
}

std::runtime_error FileError(std::string_view file, const YAML::Mark &mark, std::string_view message)
{
	return std::runtime_error{std::format(
	    "Strings: {}{}: {}", file, mark.is_null() ? "" : ":" + std::to_string(mark.line + 1), message)};
}

void Flatten(const YAML::Node &node, const std::string &prefix, const std::string &file, Entries &into)
{
	for (const auto &pair : node)
	{
		const auto name = pair.first.as<std::string>();
		// The root's version is the file's, not a text.
		if (prefix.empty() && name == "version")
			continue;
		std::string key = prefix;
		key.append(prefix.empty() ? "" : ".").append(name);
		const YAML::Node &value = pair.second;
		if (value.IsMap())
			Flatten(value, key, file, into);
		else if (value.IsScalar())
			into.insert_or_assign(key, Entry{value.as<std::string>(), file});
		else
			throw FileError(
			    file, value.Mark(), std::format("'{}' is neither a text nor a mapping of them", key));
	}
}

// One file of a language, every mount's merged into the one below it, key by key.
Entries ReadFile(const Vfs::FileSystem &files, const std::string &path)
{
	Entries entries;
	for (const Vfs::Layer &layer : files.ReadLayers(path))
	{
		const std::string file = std::format("{}:{}", files.Mounts()[layer.mount].name, path);
		YAML::Node root;
		try
		{
			root = YAML::Load(
			    std::string{reinterpret_cast<const char *>(layer.bytes.data()), layer.bytes.size()});
		}
		catch (const YAML::Exception &error)
		{
			throw FileError(file, error.mark, error.msg);
		}
		if (!root.IsMap())
			throw FileError(file, root.Mark(), "a table is a mapping, with its version");
		const YAML::Node version = root["version"];
		if (!version || !version.IsScalar() ||
		    version.as<std::string>() != std::to_string(kFormatVersion))
			throw FileError(file, root.Mark(), std::format("a table is of version {}", kFormatVersion));
		const std::size_t before = entries.size();
		Entries layerEntries;
		Flatten(root, {}, file, layerEntries);
		std::size_t changed = 0;
		for (auto &[key, entry] : layerEntries)
		{
			changed += entries.contains(key) ? 1 : 0;
			entries.insert_or_assign(key, std::move(entry));
		}
		if (before != 0)
			Core::Log(
			    Core::Severity::Info, kStrings, "{} changes {} and adds {} string(s)", file, changed,
			    entries.size() - before);
	}
	return entries;
}

Entries ReadLanguage(const Vfs::FileSystem &files, std::string_view language)
{
	Entries entries;
	const std::string directory = std::format("{}/{}", StringTable::kDirectory, language);
	for (const std::string &path : files.Enumerate(directory))
	{
		if (!path.ends_with(kExtension))
			continue;
		for (auto &[key, entry] : ReadFile(files, path))
		{
			const auto found = entries.find(key);
			if (found != entries.end())
				throw std::runtime_error{std::format(
				    "Strings: '{}' is in both {} and {}", key, found->second.file, entry.file)};
			entries.emplace(key, std::move(entry));
		}
	}
	return entries;
}

// The base's text a translation of the key is held to: the key's, or for a plural form the base
// does not use (Russian's "few", where English has "one" and "other") the base's "other".
const Entry *BaseFor(const Entries &base, std::string_view key)
{
	if (const auto found = base.find(key); found != base.end())
		return &found->second;
	const std::size_t dot = key.rfind('.');
	if (dot == std::string_view::npos ||
	    std::ranges::find(kCategories, key.substr(dot + 1)) == kCategories.end())
		return nullptr;
	const auto other = base.find(std::string{key.substr(0, dot)} + ".other");
	return other != base.end() ? &other->second : nullptr;
}

bool Within(const References &translation, const References &base)
{
	return std::ranges::includes(base.arguments, translation.arguments) &&
	       std::ranges::includes(base.expressions, translation.expressions);
}
} // namespace

void StringTable::Load(const Vfs::FileSystem &files, std::string_view language)
{
	if (!IsLanguageTag(language))
		throw std::runtime_error{
		    std::format("Strings: '{}' is not a language, as \"de\" or \"pt-BR\" is", language)};

	Entries base = ReadLanguage(files, kBaseLanguage);
	// A build that shipped without its tables would otherwise show every key as itself.
	if (base.empty())
		throw std::runtime_error{std::format(
		    "Strings: there are no tables for the base language under {}/{}/", kDirectory,
		    kBaseLanguage)};
	for (const auto &[key, entry] : base)
		if (!FindReferences(entry.text))
			throw std::runtime_error{std::format(
			    "Strings: {} has '{}' with a brace that is not an argument or a data expression",
			    entry.file, key)};

	std::vector<std::string> chain;
	const std::string_view primary = language.substr(0, language.find('-'));
	if (primary != kBaseLanguage)
		chain.emplace_back(primary);
	if (language != primary)
		chain.emplace_back(language);

	std::map<std::string, std::string, std::less<>> texts;
	for (auto &[key, entry] : base)
		texts.emplace(key, entry.text);
	std::size_t problems = 0;
	std::set<std::string, std::less<>> translated;
	bool any = chain.empty();
	for (const std::string &level : chain)
	{
		Entries entries = ReadLanguage(files, level);
		any = any || !entries.empty();
		for (auto &[key, entry] : entries)
		{
			const Entry *pBase = BaseFor(base, key);
			const std::optional<References> references = FindReferences(entry.text);
			std::string_view refusal;
			if (!pBase)
				refusal = "is a key the base language does not have";
			else if (!references)
				refusal = "has a brace that is not an argument or a data expression";
			else if (!Within(*references, *FindReferences(pBase->text)))
				refusal = "names an argument or a data expression its base text does not";
			if (!refusal.empty())
			{
				Core::Log(
				    Core::Severity::Warning, kStrings, "{}: '{}' {}, and is not read", entry.file, key,
				    refusal);
				++problems;
				continue;
			}
			if (base.contains(key))
				translated.insert(key);
			texts.insert_or_assign(key, std::move(entry.text));
		}
	}

	m_language = std::string{language};
	m_texts = std::move(texts);
	m_missing.clear();
	m_problems = problems;
	if (!any)
		Core::Log(
		    Core::Severity::Warning, kStrings, "There are no tables for '{}': the text is {}'s",
		    language, kBaseLanguage);
	else
		Core::Log(
		    Core::Severity::Info, kStrings, "{} string(s) in '{}'{}", m_texts.size(), m_language,
		    chain.empty()
		        ? std::string{}
		        : std::format(", {} of them translated from '{}'", translated.size(), kBaseLanguage));
}

std::vector<std::string> StringTable::Languages(const Vfs::FileSystem &files)
{
	std::set<std::string, std::less<>> languages;
	for (const std::string &path : files.Enumerate(kDirectory))
	{
		const std::string_view rest = std::string_view{path}.substr(kDirectory.size() + 1);
		const std::size_t slash = rest.find('/');
		if (slash != std::string_view::npos && rest.substr(slash).ends_with(kExtension))
			languages.emplace(rest.substr(0, slash));
	}
	return {languages.begin(), languages.end()};
}

std::optional<std::string_view> StringTable::Find(std::string_view key) const
{
	const auto found = m_texts.find(key);
	if (found == m_texts.end())
		return std::nullopt;
	return found->second;
}

std::string_view StringTable::Text(std::string_view key) const
{
	if (const auto found = m_texts.find(key); found != m_texts.end())
		return found->second;
	if (m_missing.emplace(key).second)
	{
		++m_problems;
		Core::Log(Core::Severity::Warning, kStrings, "No table has the string '{}'", key);
	}
	return *m_missing.find(key);
}

std::string_view StringTable::PluralText(std::string_view key, std::int64_t count) const
{
	const std::string form = std::format("{}.{}", key, PluralCategory(m_language, count));
	if (const auto found = m_texts.find(form); found != m_texts.end())
		return found->second;
	return Text(std::format("{}.other", key));
}

std::string StringTable::FormatText(
    std::string_view key, std::string_view text, std::format_args arguments) const
{
	try
	{
		return std::vformat(text, arguments);
	}
	catch (const std::format_error &error)
	{
		if (m_missing.emplace(std::format("{} (format)", key)).second)
		{
			++m_problems;
			Core::Log(
			    Core::Severity::Warning, kStrings, "The string '{}' does not format its arguments: {}",
			    key, error.what());
		}
		return std::string{text};
	}
}

std::string StringTable::Number(std::int64_t value) const
{
	const std::string_view separator = Find("numbers.thousands").value_or(",");
	const std::uint64_t magnitude =
	    value < 0 ? 0 - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
	std::string digits = std::to_string(magnitude);
	for (auto at = static_cast<std::ptrdiff_t>(digits.size()) - 3; at > 0; at -= 3)
		digits.insert(static_cast<std::size_t>(at), separator);
	return value < 0 ? "-" + digits : digits;
}

std::string StringTable::Translate(std::string_view text, int &replaced) const
{
	replaced = 0;
	std::string result;
	std::size_t at = 0;
	while (at < text.size())
	{
		const std::size_t open = text.find("[[", at);
		const std::size_t close = open == std::string_view::npos ? open : text.find("]]", open + 2);
		if (close == std::string_view::npos)
			break;
		const std::string_view key = text.substr(open + 2, close - open - 2);
		const bool isKey =
		    !key.empty() && std::ranges::all_of(
		                        key,
		                        [](char c)
		                        {
			                        return std::isalnum(static_cast<unsigned char>(c)) != 0 ||
			                               c == '.' || c == '_' || c == '-';
		                        });
		result.append(text.substr(at, open - at));
		if (isKey)
		{
			result.append(Text(key));
			++replaced;
		}
		else
			result.append(text.substr(open, close + 2 - open));
		at = close + 2;
	}
	result.append(text.substr(std::min(at, text.size())));
	return result;
}

std::string_view StringTable::PluralCategory(std::string_view language, std::int64_t count)
{
	// The CLDR's rules for whole numbers, for the languages a game is most often translated into;
	// every other language has English's.
	const std::string_view primary = language.substr(0, language.find('-'));
	const std::uint64_t n =
	    count < 0 ? 0 - static_cast<std::uint64_t>(count) : static_cast<std::uint64_t>(count);
	const std::uint64_t ten = n % 10;
	const std::uint64_t hundred = n % 100;
	const bool fewEnding = ten >= 2 && ten <= 4 && (hundred < 12 || hundred > 14);
	constexpr std::array<std::string_view, 8> kNone{"ja", "zh", "ko", "vi", "th", "id", "ms", "tr"};
	if (std::ranges::find(kNone, primary) != kNone.end())
		return "other";
	if (primary == "fr" || (primary == "pt" && language != "pt-PT"))
		return n <= 1 ? "one" : "other";
	if (primary == "ru" || primary == "uk" || primary == "be")
		return ten == 1 && hundred != 11 ? "one" : fewEnding ? "few" : "many";
	if (primary == "pl")
		return n == 1 ? "one" : fewEnding ? "few" : "many";
	if (primary == "cs" || primary == "sk")
		return n == 1 ? "one" : n >= 2 && n <= 4 ? "few" : "other";
	return n == 1 ? "one" : "other";
}
} // namespace Jettison::Ui
