#pragma once

#include <filesystem>
#include <span>
#include <string>

#include <jettison/core/Config.h>
#include <jettison/input/Joysticks.h>

namespace Jettison::Input
{
class InputSystem;
}

namespace Jettison::App
{
// The user's settings and bindings: one YAML file in the configuration directory
// (Platform::Paths::config, ~/.config/jettison/settings.yaml), apart from the saves in the data
// directory, so a save carries none of them and loading one changes none. The file is the config's
// user layer; the bindings are the input system's overrides in it (input.<context>.<action>), and
// beside them the devices the player's joysticks were bound as (Input::JoystickMap). The file is
// written whole and durably (Core::WriteFileDurably) each time a setting changes, so what the
// player changed is there after a restart, a crash or a power cut.
class Settings
{
  public:
	explicit Settings(std::filesystem::path file);

	// Reads the file, then the bindings and the joysticks' names from it, applying them to the input
	// system. A file that does not read, or holds bindings that are not, is reported, and nothing in
	// it is applied: the defaults stand. The file is written only when the player changes a setting,
	// and the first write after such a load keeps the file it replaces beside it as
	// settings.yaml.unread, so a mistake in an edit by hand is not lost with the rest of the file.
	void Load(Input::InputSystem &input);
	// The bindings and the joysticks' names into the user layer, and the layer to the file. Throws
	// std::runtime_error naming the file when it cannot be written.
	void Save(const Input::InputSystem &input);

	Core::Config &Values()
	{
		return m_config;
	}
	Input::JoystickMap &Joysticks()
	{
		return m_joysticks;
	}
	const Input::JoystickMap &Joysticks() const
	{
		return m_joysticks;
	}
	const std::filesystem::path &File() const
	{
		return m_file;
	}

  private:
	std::filesystem::path m_file;
	// The file did not read, and has not been put aside yet.
	bool m_unread{false};
	Core::Config m_config;
	Input::JoystickMap m_joysticks;
};

// The console's "bind <context> <action> [bindings...]": with bindings, in FormatBindings' text
// (Key.W/Key.S, y:GamepadAxis.LeftY*-1~0.15; several arguments are several bindings), rebinds the
// action; without, says what it is bound to. Returns what the console prints, and whether the
// bindings changed. Throws std::runtime_error, with the reason, for a context or an action that is
// not defined, text that is not bindings, or a binding that does not suit the action.
struct BindResult
{
	std::string message;
	bool changed{false};
};
BindResult Bind(Input::InputSystem &input, std::span<const std::string> arguments);
// The console's "unbind <context> <action>": back to the defaults.
BindResult Unbind(Input::InputSystem &input, std::span<const std::string> arguments);
} // namespace Jettison::App
