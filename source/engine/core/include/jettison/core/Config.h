#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Jettison::Core
{
// Layered configuration: engine defaults, then the user settings file, then the command line,
// each overriding the one before. Keys are dotted paths ("render.vsync", "audio.music"); in a file
// each segment is a level of a YAML map. Values are stored as text and read as the type asked
// for. Settings and keybindings are stored apart from saves (docs/roadmap.md, entry 25); where
// the user file lives is the platform module's answer (Paths::config), not this class's.
//
// Not thread-safe: it is read and written on the main thread.
enum class ConfigLayer : std::uint8_t
{
	Defaults,
	User,
	CommandLine
};

class Config
{
  public:
	// Reads a YAML file into a layer, over what the layer already holds. A missing file is not an
	// error; one that cannot be checked or read, is malformed, or is not a map of maps and scalars
	// throws naming the path, and leaves the layer as it was.
	void LoadLayer(ConfigLayer layer, const std::filesystem::path &path);

	// Throws std::invalid_argument for a key that is not dotted segments of letters, digits and
	// underscores, or that would make one of the layer's keys both a value and a map ("render" and
	// "render.vsync"): the file format cannot hold that.
	void Set(ConfigLayer layer, std::string_view key, std::string value);
	void SetNumber(ConfigLayer layer, std::string_view key, double value);
	void SetBool(ConfigLayer layer, std::string_view key, bool value);

	// The value from the highest layer that has the key in the type asked for. A layer whose value
	// does not read as that type is skipped with a warning, so a bad edit to the user file falls
	// back to the default rather than to nothing.
	std::optional<std::string> GetString(std::string_view key) const;
	std::optional<double> GetNumber(std::string_view key) const;
	// "true" or "false" only.
	std::optional<bool> GetBool(std::string_view key) const;

	// Writes the user layer as YAML, leaving out values equal to the default's. Throws naming the
	// path when it cannot be written.
	void SaveUserLayer(const std::filesystem::path &path) const;

  private:
	using Values = std::map<std::string, std::string, std::less<>>;

	// Highest first, the order Get searches them in.
	template <typename T>
	std::optional<T> Find(
	    std::string_view key, std::string_view type, std::optional<T> (*parse)(std::string_view)) const;

	std::array<Values, 3> m_layers;
};
} // namespace Jettison::Core
