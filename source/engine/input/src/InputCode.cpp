#include <jettison/input/InputCode.h>

#include <array>
#include <charconv>
#include <format>
#include <memory>
#include <span>
#include <utility>

namespace Jettison::Input
{
namespace
{
struct Named
{
	std::int32_t code;
	std::string_view name;
};

// GLFW's names, without the prefixes. Letters, digits and F-keys are made by rule below.
constexpr std::array kKeys{
    Named{32, "Space"},
    Named{39, "Apostrophe"},
    Named{44, "Comma"},
    Named{45, "Minus"},
    Named{46, "Period"},
    Named{47, "Slash"},
    Named{59, "Semicolon"},
    Named{61, "Equal"},
    Named{91, "LeftBracket"},
    Named{92, "Backslash"},
    Named{93, "RightBracket"},
    Named{96, "GraveAccent"},
    Named{256, "Escape"},
    Named{257, "Enter"},
    Named{258, "Tab"},
    Named{259, "Backspace"},
    Named{260, "Insert"},
    Named{261, "Delete"},
    Named{262, "Right"},
    Named{263, "Left"},
    Named{264, "Down"},
    Named{265, "Up"},
    Named{266, "PageUp"},
    Named{267, "PageDown"},
    Named{268, "Home"},
    Named{269, "End"},
    Named{280, "CapsLock"},
    Named{281, "ScrollLock"},
    Named{282, "NumLock"},
    Named{283, "PrintScreen"},
    Named{284, "Pause"},
    Named{330, "KeypadDecimal"},
    Named{331, "KeypadDivide"},
    Named{332, "KeypadMultiply"},
    Named{333, "KeypadSubtract"},
    Named{334, "KeypadAdd"},
    Named{335, "KeypadEnter"},
    Named{336, "KeypadEqual"},
    Named{340, "LeftShift"},
    Named{341, "LeftControl"},
    Named{342, "LeftAlt"},
    Named{343, "LeftSuper"},
    Named{344, "RightShift"},
    Named{345, "RightControl"},
    Named{346, "RightAlt"},
    Named{347, "RightSuper"},
    Named{348, "Menu"},
};
constexpr std::array kMouseButtons{Named{0, "Left"}, Named{1, "Right"}, Named{2, "Middle"}};
constexpr std::array kMouseAxes{Named{0, "X"}, Named{1, "Y"}, Named{2, "WheelX"}, Named{3, "WheelY"}};
constexpr std::array kGamepadButtons{
    Named{0, "A"},          Named{1, "B"},           Named{2, "X"},           Named{3, "Y"},
    Named{4, "LeftBumper"}, Named{5, "RightBumper"}, Named{6, "Back"},        Named{7, "Start"},
    Named{8, "Guide"},      Named{9, "LeftThumb"},   Named{10, "RightThumb"}, Named{11, "DpadUp"},
    Named{12, "DpadRight"}, Named{13, "DpadDown"},   Named{14, "DpadLeft"},
};
constexpr std::array kGamepadAxes{Named{0, "LeftX"},       Named{1, "LeftY"},
                                  Named{2, "RightX"},      Named{3, "RightY"},
                                  Named{4, "LeftTrigger"}, Named{5, "RightTrigger"}};

struct DeviceNames
{
	Device device;
	std::string_view prefix;
	std::span<const Named> names;
};

constexpr std::array kDevices{
    DeviceNames{Device::Keyboard, "Key", kKeys},
    DeviceNames{Device::MouseButton, "Mouse", kMouseButtons},
    DeviceNames{Device::MouseAxis, "MouseAxis", kMouseAxes},
    DeviceNames{Device::GamepadButton, "Gamepad", kGamepadButtons},
    DeviceNames{Device::GamepadAxis, "GamepadAxis", kGamepadAxes},
};

template <typename T> bool ParseWhole(std::string_view text, T &value)
{
	const char *const first = std::to_address(text.begin());
	const char *const last = std::to_address(text.end());
	const auto [stop, error] = std::from_chars(first, last, value);
	return !text.empty() && error == std::errc{} && stop == last;
}

std::string FormatNumber(float value)
{
	std::array<char, 32> buffer{};
	const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	return {buffer.data(), result.ptr};
}

std::string_view Trim(std::string_view text)
{
	while (!text.empty() && text.front() == ' ')
		text.remove_prefix(1);
	while (!text.empty() && text.back() == ' ')
		text.remove_suffix(1);
	return text;
}

std::optional<Binding> ParseBinding(std::string_view text)
{
	Binding binding;
	if (text.starts_with("x:") || text.starts_with("y:"))
	{
		binding.component = text.front() == 'x' ? 0 : 1;
		text.remove_prefix(2);
	}
	if (const std::size_t tilde = text.find('~'); tilde != std::string_view::npos)
	{
		if (!ParseWhole(text.substr(tilde + 1), binding.deadZone) || binding.deadZone < 0.0f)
			return std::nullopt;
		text = text.substr(0, tilde);
	}
	if (const std::size_t star = text.find('*'); star != std::string_view::npos)
	{
		if (!ParseWhole(text.substr(star + 1), binding.scale))
			return std::nullopt;
		text = text.substr(0, star);
	}
	if (const std::size_t slash = text.find('/'); slash != std::string_view::npos)
	{
		binding.negative = ParseInput(text.substr(slash + 1));
		if (!binding.negative)
			return std::nullopt;
		text = text.substr(0, slash);
	}
	const std::optional<InputCode> input = ParseInput(text);
	if (!input)
		return std::nullopt;
	binding.input = *input;
	return binding;
}
} // namespace

std::string FormatInput(InputCode input)
{
	if (input.device == Device::JoystickAxis || input.device == Device::JoystickButton)
		return std::format(
		    "Joystick{}.{}{}", input.joystick + 1,
		    input.device == Device::JoystickAxis ? "Axis" : "Button", input.code);
	for (const DeviceNames &device : kDevices)
	{
		if (device.device != input.device)
			continue;
		if (input.device == Device::Keyboard)
		{
			if ((input.code >= 'A' && input.code <= 'Z') || (input.code >= '0' && input.code <= '9'))
				return std::format("Key.{}", static_cast<char>(input.code));
			if (input.code >= 290 && input.code <= 314)
				return std::format("Key.F{}", input.code - 289);
			if (input.code >= 320 && input.code <= 329)
				return std::format("Key.Keypad{}", input.code - 320);
		}
		for (const Named &named : device.names)
		{
			if (named.code == input.code)
				return std::format("{}.{}", device.prefix, named.name);
		}
		return std::format("{}.#{}", device.prefix, input.code);
	}
	return std::format("#{}.#{}", static_cast<int>(input.device), input.code);
}

std::optional<InputCode> ParseInput(std::string_view text)
{
	const std::size_t dot = text.find('.');
	if (dot == std::string_view::npos)
		return std::nullopt;
	const std::string_view prefix = text.substr(0, dot);
	const std::string_view name = text.substr(dot + 1);
	if (prefix.starts_with("Joystick"))
	{
		unsigned number = 0;
		if (!ParseWhole(prefix.substr(8), number) || number < 1 || number > kJoystickCount)
			return std::nullopt;
		const auto joystick = static_cast<std::uint8_t>(number - 1);
		std::int32_t code = 0;
		for (const auto [word, device] :
		     {std::pair{std::string_view{"Axis"}, Device::JoystickAxis},
		      std::pair{std::string_view{"Button"}, Device::JoystickButton}})
			if (name.starts_with(word) && ParseWhole(name.substr(word.size()), code) && code >= 0)
				return InputCode{device, code, joystick};
		return std::nullopt;
	}
	for (const DeviceNames &device : kDevices)
	{
		if (device.prefix != prefix)
			continue;
		if (name.starts_with('#'))
		{
			std::int32_t code = 0;
			if (!ParseWhole(name.substr(1), code))
				return std::nullopt;
			return InputCode{device.device, code};
		}
		if (device.device == Device::Keyboard)
		{
			if (name.size() == 1 &&
			    ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= '0' && name[0] <= '9')))
				return InputCode{Device::Keyboard, name[0]};
			std::int32_t number = 0;
			if (name.starts_with('F') && ParseWhole(name.substr(1), number) && number >= 1 &&
			    number <= 25)
				return InputCode{Device::Keyboard, 289 + number};
			if (name.starts_with("Keypad") && name.size() == 7 && name[6] >= '0' && name[6] <= '9')
				return InputCode{Device::Keyboard, 320 + (name[6] - '0')};
		}
		for (const Named &named : device.names)
		{
			if (named.name == name)
				return InputCode{device.device, named.code};
		}
		return std::nullopt;
	}
	return std::nullopt;
}

std::string FormatBindings(const std::vector<Binding> &bindings)
{
	std::string text;
	for (const Binding &binding : bindings)
	{
		if (!text.empty())
			text += ", ";
		if (binding.component != 0)
			text += "y:";
		text += FormatInput(binding.input);
		if (binding.negative)
			text += "/" + FormatInput(*binding.negative);
		if (binding.scale != 1.0f)
			text += "*" + FormatNumber(binding.scale);
		if (binding.deadZone != 0.0f)
			text += "~" + FormatNumber(binding.deadZone);
	}
	return text;
}

std::optional<std::vector<Binding>> ParseBindings(std::string_view text)
{
	std::vector<Binding> bindings;
	text = Trim(text);
	if (text.empty())
		return bindings;
	for (;;)
	{
		const std::size_t comma = text.find(',');
		const std::optional<Binding> binding = ParseBinding(Trim(text.substr(0, comma)));
		if (!binding)
			return std::nullopt;
		bindings.push_back(*binding);
		if (comma == std::string_view::npos)
			return bindings;
		text.remove_prefix(comma + 1);
	}
}
} // namespace Jettison::Input
