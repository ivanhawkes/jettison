#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <jettison/input/InputCode.h>

namespace Jettison::Core
{
class Config;
}

namespace Jettison::Input
{
class InputSystem;

// A joystick connected now: GLFW's slot for it, and its GUID, which is the same for the same model
// of device wherever and whenever it is plugged in.
struct JoystickDevice
{
	std::uint8_t slot{0};
	std::string guid;
};

// Which device a binding's joystick is. A binding names a joystick by number (InputCode::joystick,
// "Joystick2" in text), and a number the player has bound is named after the device it was then:
// from then on the number is that device wherever it is plugged in, so a HOTAS's throttle bound as
// Joystick2 stays the throttle when the stick is unplugged and the throttle moves to GLFW's first
// slot. Numbers no name claims are the devices no name claims, in slot order, which with nothing
// named is GLFW's slots as they are.
//
// Two devices of one model share a GUID; a name then claims the first of them in slot order that no
// lower number has claimed.
//
// Names are kept in the user's settings, beside the bindings, under "input.devices.joystick<N>"
// (numbered from 1, as the text of a binding has it). Main thread only.
class JoystickMap
{
  public:
	// Throws std::invalid_argument for a number past the last joystick or a GUID that is not hex
	// digits.
	void Name(std::uint8_t joystick, std::string guid);
	void Forget(std::uint8_t joystick);
	// The device the number is named after, or nothing.
	std::optional<std::string_view> NameOf(std::uint8_t joystick) const;

	// For each number, the slot of the device that answers to it now, or nothing.
	std::array<std::optional<std::uint8_t>, kJoystickCount> Assign(
	    std::span<const JoystickDevice> connected) const;

	// Names each joystick a binding the player chose binds (the input system's overrides, not its
	// defaults), and that has no name yet, after the device that answers to it now. A number bound
	// with nothing connected to answer stays unnamed. Returns whether a name was added.
	bool NameBound(const InputSystem &input, std::span<const JoystickDevice> connected);

	// The names into the user layer, replacing what it has under input.devices.
	void Save(Core::Config &config) const;
	// Takes every input.devices.joystick<N> the config has, replacing the names held. Throws
	// std::runtime_error naming the key for one that is not a GUID; nothing is changed then.
	void Load(const Core::Config &config);

  private:
	std::map<std::uint8_t, std::string> m_names;
};

// "input.devices.joystick<N>", numbered from 1.
std::string JoystickNameKey(std::uint8_t joystick);
} // namespace Jettison::Input
