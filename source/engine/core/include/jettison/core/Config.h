#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace Jettison::Core
{
// Layered configuration: engine defaults, then a user settings file, then the command line, each
// overriding the one before. Keys are dotted paths ("render.vsync", "audio.music"). Settings and
// keybindings are stored apart from saves (docs/roadmap.md, entry 25). Filled in phase 2, when
// the platform module says where the user file lives.
class Config
{
  public:
	// Reads a YAML file as the next layer; a missing file is not an error, a malformed one is.
	void LoadLayer(const std::filesystem::path &path);
	void Set(std::string_view key, std::string value);

	std::optional<std::string> GetString(std::string_view key) const;
	std::optional<double> GetNumber(std::string_view key) const;
	std::optional<bool> GetBool(std::string_view key) const;

	// Writes the user layer only: values that differ from the defaults.
	void SaveUserLayer(const std::filesystem::path &path) const;
};
} // namespace Jettison::Core
