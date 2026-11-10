#include <jettison/core/Config.h>

#include <yaml-cpp/yaml.h>

#include <charconv>
#include <format>
#include <fstream>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <jettison/core/Log.h>

namespace Jettison::Core
{
namespace
{
constexpr Category kConfigLog{"config"};

bool IsKeyCharacter(char character)
{
	return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
	       (character >= '0' && character <= '9') || character == '_';
}

// The one rule for a key, which the loader and Set both apply: dotted segments, none empty, of
// letters, digits and underscores. It is what lets a key be a path through nested YAML maps and
// come back from the file as the same key.
bool IsValidKey(std::string_view key)
{
	if (key.empty() || key.front() == '.' || key.back() == '.')
		return false;
	char previous = '\0';
	for (const char character : key)
	{
		if (character == '.' ? previous == '.' : !IsKeyCharacter(character))
			return false;
		previous = character;
	}
	return true;
}

// Whether one of the two keys is a map the other is a value in: "render" and "render.vsync".
bool IsPrefixPath(std::string_view shorter, std::string_view longer)
{
	return longer.size() > shorter.size() && longer.starts_with(shorter) &&
	       longer[shorter.size()] == '.';
}

// The one place a value enters a layer, so Set and LoadLayer refuse the same keys.
void Insert(
    std::map<std::string, std::string, std::less<>> &values, std::string_view key, std::string value)
{
	if (!IsValidKey(key))
		throw std::invalid_argument{std::format("Config: '{}' is not a valid key", key)};
	for (const auto &[existing, existingValue] : values)
	{
		if (IsPrefixPath(existing, key) || IsPrefixPath(key, existing))
			throw std::invalid_argument{
			    std::format("Config: '{}' and '{}' cannot both hold values", key, existing)};
	}
	values.insert_or_assign(std::string{key}, std::move(value));
}

std::optional<std::string> ParseString(std::string_view text)
{
	return std::string{text};
}

std::optional<double> ParseNumber(std::string_view text)
{
	double value = 0.0;
	const char *const first = std::to_address(text.begin());
	const char *const last = std::to_address(text.end());
	const auto [stop, error] = std::from_chars(first, last, value);
	if (error != std::errc{} || stop != last)
		return std::nullopt;
	return value;
}

std::optional<bool> ParseBool(std::string_view text)
{
	if (text == "true")
		return true;
	if (text == "false")
		return false;
	return std::nullopt;
}

std::size_t Index(ConfigLayer layer)
{
	return static_cast<std::size_t>(layer);
}

[[noreturn]] void ThrowMalformed(const std::filesystem::path &path, const std::string &reason)
{
	throw std::runtime_error{std::format("Config {}: {}", path.string(), reason)};
}

// Flattens a YAML map into dotted keys. Anything other than maps and scalars is not a setting.
void Flatten(
    const YAML::Node &node, const std::string &prefix, const std::filesystem::path &path,
    std::map<std::string, std::string, std::less<>> &into)
{
	for (const auto &entry : node)
	{
		const auto name = entry.first.as<std::string>();
		const std::string key = prefix.empty() ? name : std::format("{}.{}", prefix, name);
		if (name.find('.') != std::string::npos || !IsValidKey(key))
			ThrowMalformed(path, std::format("'{}' is not a valid key", key));
		if (entry.second.IsMap())
			Flatten(entry.second, key, path, into);
		else if (entry.second.IsScalar())
			into.insert_or_assign(key, entry.second.Scalar());
		else
			ThrowMalformed(path, std::format("'{}' is not a value or a map of values", key));
	}
}
} // namespace

void Config::LoadLayer(ConfigLayer layer, const std::filesystem::path &path)
{
	// Missing is an empty layer. Unreadable is not: a settings file the user cannot see would be
	// replaced by the next save.
	std::error_code error;
	if (!std::filesystem::exists(path, error))
	{
		if (error)
			ThrowMalformed(path, error.message());
		return;
	}

	// Into a copy, so a file that fails part of the way through leaves the layer as it was.
	Values merged = m_layers[Index(layer)];
	try
	{
		const YAML::Node root = YAML::LoadFile(path.string());
		if (root.IsNull())
			return; // An empty file is an empty layer.
		if (!root.IsMap())
			ThrowMalformed(path, "the top level is not a map");
		Values loaded;
		Flatten(root, "", path, loaded);
		for (auto &[key, value] : loaded)
			Insert(merged, key, std::move(value));
	}
	catch (const YAML::Exception &exception)
	{
		ThrowMalformed(path, exception.what());
	}
	catch (const std::invalid_argument &exception)
	{
		ThrowMalformed(path, exception.what());
	}
	m_layers[Index(layer)] = std::move(merged);
}

void Config::Set(ConfigLayer layer, std::string_view key, std::string value)
{
	Insert(m_layers[Index(layer)], key, std::move(value));
}

void Config::SetNumber(ConfigLayer layer, std::string_view key, double value)
{
	// std::format's default for a double is the shortest text that reads back as the same value.
	Set(layer, key, std::format("{}", value));
}

void Config::SetBool(ConfigLayer layer, std::string_view key, bool value)
{
	Set(layer, key, value ? "true" : "false");
}

template <typename T>
std::optional<T> Config::Find(
    std::string_view key, std::string_view type, std::optional<T> (*parse)(std::string_view)) const
{
	for (const Values &layer : std::views::reverse(m_layers))
	{
		const auto entry = layer.find(key);
		if (entry == layer.end())
			continue;
		if (auto value = parse(entry->second))
			return value;
		Log(Severity::Warning, kConfigLog, "'{}' is '{}', which is not a {}; using the layer below",
		    key, entry->second, type);
	}
	return std::nullopt;
}

std::optional<std::string> Config::GetString(std::string_view key) const
{
	return Find<std::string>(key, "string", ParseString);
}

std::optional<double> Config::GetNumber(std::string_view key) const
{
	return Find<double>(key, "number", ParseNumber);
}

std::optional<bool> Config::GetBool(std::string_view key) const
{
	return Find<bool>(key, "bool", ParseBool);
}

void Config::SaveUserLayer(const std::filesystem::path &path) const
{
	const Values &defaults = m_layers[Index(ConfigLayer::Defaults)];

	// The keys come out of the map sorted, and every character a key may hold sorts after '.', so
	// the keys under one map are adjacent: the emitter opens a map when a key enters it and closes
	// it when the next key leaves it. Insert has refused any key that would need one segment to be
	// a value and a map at once.
	YAML::Emitter emitter;
	emitter << YAML::BeginMap;
	std::vector<std::string_view> open;
	for (const auto &[key, value] : m_layers[Index(ConfigLayer::User)])
	{
		if (const auto entry = defaults.find(key); entry != defaults.end() && entry->second == value)
			continue;

		std::vector<std::string_view> segments;
		for (std::string_view rest = key;;)
		{
			const auto dot = rest.find('.');
			segments.push_back(rest.substr(0, dot));
			if (dot == std::string_view::npos)
				break;
			rest.remove_prefix(dot + 1);
		}

		std::size_t shared = 0;
		while (shared < open.size() && shared + 1 < segments.size() && open[shared] == segments[shared])
			++shared;
		for (; open.size() > shared; open.pop_back())
			emitter << YAML::EndMap;
		for (; open.size() + 1 < segments.size(); open.push_back(segments[open.size()]))
			emitter << YAML::Key << std::string{segments[open.size()]} << YAML::Value << YAML::BeginMap;
		emitter << YAML::Key << std::string{segments.back()} << YAML::Value << value;
	}
	for (; !open.empty(); open.pop_back())
		emitter << YAML::EndMap;
	emitter << YAML::EndMap;
	if (!emitter.good())
		throw std::runtime_error{std::format("Config {}: {}", path.string(), emitter.GetLastError())};

	std::ofstream file{path, std::ios::trunc};
	file << emitter.c_str() << '\n';
	if (!file)
		throw std::runtime_error{std::format("Config {}: could not be written", path.string())};
}
} // namespace Jettison::Core
