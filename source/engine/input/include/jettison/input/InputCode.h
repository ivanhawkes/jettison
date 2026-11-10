#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Input
{
// Where an input comes from. Codes are GLFW's numbers (GLFW_KEY_*, GLFW_MOUSE_BUTTON_*,
// GLFW_GAMEPAD_BUTTON_*, GLFW_GAMEPAD_AXIS_*), so the shell feeds them through unchanged and
// this module does not include GLFW; the shell checks a sample of them against GLFW's headers.
// Mouse axes are this module's: 0 and 1 are the cursor's motion in pixels since the last poll, 2
// and 3 the wheel's. The gamepad is the first one connected. Joysticks that are not gamepads, a
// HOTAS among them, come with flight in phase 7.
enum class Device : std::uint8_t
{
	Keyboard,
	MouseButton,
	MouseAxis,
	GamepadButton,
	GamepadAxis,
};

struct InputCode
{
	Device device{Device::Keyboard};
	std::int32_t code{0};

	friend auto operator<=>(const InputCode &, const InputCode &) = default;
};

namespace Keys
{
inline constexpr InputCode Space{Device::Keyboard, 32};
inline constexpr InputCode Escape{Device::Keyboard, 256};
inline constexpr InputCode Enter{Device::Keyboard, 257};
inline constexpr InputCode Tab{Device::Keyboard, 258};
inline constexpr InputCode Up{Device::Keyboard, 265};
inline constexpr InputCode GraveAccent{Device::Keyboard, 96};
inline constexpr InputCode F1{Device::Keyboard, 290};
inline constexpr InputCode F2{Device::Keyboard, 291};
// A letter or digit key: GLFW's code for one is its ASCII, upper case for a letter.
constexpr InputCode Character(char character)
{
	return {Device::Keyboard, character >= 'a' && character <= 'z' ? character - 'a' + 'A' : character};
}
} // namespace Keys

// An input as text, for settings files: "Key.W", "Key.Escape", "Mouse.Left", "MouseAxis.X",
// "Gamepad.A", "GamepadAxis.LeftY". A code with no name is written by number, "Key.#161", and
// read back the same way.
std::string FormatInput(InputCode input);
std::optional<InputCode> ParseInput(std::string_view text);

// One way to drive an action.
//
// A button action is pressed while any of its bindings reads at least 0.5. An axis action is the
// sum of its bindings, clamped to [-1, 1]; a vector action sums each component without a clamp,
// so mouse motion comes through whole. A binding reads its input (1 for a button held, the value
// for an axis), minus its negative input if it has one, which is how two keys make an axis, then
// times its scale. An analog reading inside the dead zone is 0.
struct Binding
{
	InputCode input{};
	std::optional<InputCode> negative{};
	float scale{1.0f};
	float deadZone{0.0f};
	// For a vector action: 0 drives x, 1 drives y.
	std::uint8_t component{0};

	friend bool operator==(const Binding &, const Binding &) = default;
};

// Bindings as text, comma-separated: "Key.W/Key.S", "y:GamepadAxis.LeftY*-1~0.15" for component,
// scale and dead zone. The empty string is no bindings. ParseBindings returns nothing for text
// that is not bindings.
std::string FormatBindings(const std::vector<Binding> &bindings);
std::optional<std::vector<Binding>> ParseBindings(std::string_view text);
} // namespace Jettison::Input
