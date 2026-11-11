#include <jettison/input/Joysticks.h>

#include <algorithm>
#include <cctype>
#include <format>
#include <stdexcept>
#include <utility>
#include <vector>

#include <jettison/core/Config.h>
#include <jettison/input/Actions.h>

namespace Jettison::Input
{
namespace
{
bool IsGuid(std::string_view text)
{
	return !text.empty() && text.size() <= 64 &&
	       std::ranges::all_of(
	           text, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}
} // namespace

std::string JoystickNameKey(std::uint8_t joystick)
{
	return std::format("input.devices.joystick{}", unsigned{joystick} + 1);
}

void JoystickMap::Name(std::uint8_t joystick, std::string guid)
{
	if (joystick >= kJoystickCount)
		throw std::invalid_argument{
		    std::format("Input: there is no joystick {}", unsigned{joystick} + 1)};
	if (!IsGuid(guid))
		throw std::invalid_argument{std::format("Input: '{}' is not a joystick's GUID", guid)};
	m_names[joystick] = std::move(guid);
}

void JoystickMap::Forget(std::uint8_t joystick)
{
	m_names.erase(joystick);
}

std::optional<std::string_view> JoystickMap::NameOf(std::uint8_t joystick) const
{
	const auto found = m_names.find(joystick);
	if (found == m_names.end())
		return std::nullopt;
	return found->second;
}

std::array<std::optional<std::uint8_t>, kJoystickCount> JoystickMap::Assign(
    std::span<const JoystickDevice> connected) const
{
	std::vector<JoystickDevice> devices{connected.begin(), connected.end()};
	std::ranges::sort(devices, {}, &JoystickDevice::slot);
	std::vector<bool> claimed(devices.size(), false);
	std::array<std::optional<std::uint8_t>, kJoystickCount> slots{};

	// The named numbers first, lowest first, each taking the first device of its model not taken.
	for (const auto &[joystick, guid] : m_names)
	{
		for (std::size_t device = 0; device < devices.size(); ++device)
		{
			if (!claimed[device] && devices[device].guid == guid)
			{
				claimed[device] = true;
				slots[joystick] = devices[device].slot;
				break;
			}
		}
	}
	// Then the numbers no name claims take the devices left, in slot order.
	std::size_t device = 0;
	for (std::uint8_t joystick = 0; joystick < kJoystickCount; ++joystick)
	{
		if (m_names.contains(joystick))
			continue;
		while (device < devices.size() && claimed[device])
			++device;
		if (device == devices.size())
			break;
		claimed[device] = true;
		slots[joystick] = devices[device].slot;
	}
	return slots;
}

bool JoystickMap::NameBound(const InputSystem &input, std::span<const JoystickDevice> connected)
{
	const auto slots = Assign(connected);
	bool named = false;
	for (const InputCode &code : input.OverriddenInputs())
	{
		if ((code.device != Device::JoystickAxis && code.device != Device::JoystickButton) ||
		    code.joystick >= kJoystickCount || m_names.contains(code.joystick) || !slots[code.joystick])
			continue;
		const auto device = std::ranges::find(connected, *slots[code.joystick], &JoystickDevice::slot);
		if (device == connected.end() || !IsGuid(device->guid))
			continue;
		m_names[code.joystick] = device->guid;
		named = true;
	}
	return named;
}

void JoystickMap::Save(Core::Config &config) const
{
	for (std::uint8_t joystick = 0; joystick < kJoystickCount; ++joystick)
	{
		const std::string key = JoystickNameKey(joystick);
		if (const auto found = m_names.find(joystick); found != m_names.end())
			config.Set(Core::ConfigLayer::User, key, found->second);
		else
			config.Unset(Core::ConfigLayer::User, key);
	}
}

void JoystickMap::Load(const Core::Config &config)
{
	std::map<std::uint8_t, std::string> names;
	for (std::uint8_t joystick = 0; joystick < kJoystickCount; ++joystick)
	{
		const std::string key = JoystickNameKey(joystick);
		std::optional<std::string> guid = config.GetString(key);
		if (!guid)
			continue;
		if (!IsGuid(*guid))
			throw std::runtime_error{std::format("Input: {} is '{}', which is not a GUID", key, *guid)};
		names[joystick] = std::move(*guid);
	}
	m_names = std::move(names);
}
} // namespace Jettison::Input
