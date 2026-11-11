#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace Jettison::Render
{
// Which GPU the renderer draws with, chosen from what each device says of itself, with no Vulkan type
// in it, so the rule is tested without a device.
//
// A machine may have more than one, and not every one is the one to draw with: a second card kept
// for compute with no monitor on it, an NVIDIA card under Xvfb that cannot present to its windows at
// all, a laptop's discrete GPU behind its integrated one. A device is ruled out for what it cannot do
// (present to this window, the API version, the features and extensions the renderer needs), and of
// the rest the one with a monitor connected is preferred, then a discrete GPU over an integrated one
// over a virtual one over the CPU, then the order Vulkan lists them in, which Mesa's device-select
// layer puts the display's GPU first in. A name the player asks for (--gpu) is taken over all of
// that, when it is a device that can be used.
enum class DeviceType : std::uint8_t
{
	Discrete,
	Integrated,
	Virtual,
	Cpu,
	Other,
};

struct DeviceCandidate
{
	std::string name;
	DeviceType type{DeviceType::Other};
	// Why it cannot be drawn with, or empty when it can.
	std::string unsuitable;
	// Whether a monitor is connected to it, as its DRM card's connectors say; nothing when that cannot
	// be told (a CPU device has no card; a driver may not say which card it is).
	std::optional<bool> drivesDisplay;
};

struct DeviceChoice
{
	// Into the candidates, or nothing when none can be drawn with.
	std::optional<std::size_t> index;
	// Why it was chosen, or why nothing was, for the log and the error.
	std::string reason;
};

// The device to draw with. requested, when not empty, is part of a device's name, matched without
// regard to case: the first usable device whose name has it is chosen, and none is when no usable
// device has it, since a player who asked for one GPU does not want another without a word.
DeviceChoice ChooseDevice(std::span<const DeviceCandidate> candidates, std::string_view requested = {});

// Whether a monitor is connected to the DRM card behind a device node (the major and minor numbers
// VK_EXT_physical_device_drm gives): any of its connectors (sysfs's <card>-<connector>/status) reads
// "connected". Nothing when the node is not a card sysfs knows. sysfs is the root to read, /sys but in
// a test.
std::optional<bool> DrivesDisplay(
    std::int64_t major, std::int64_t minor, const std::filesystem::path &sysfs = "/sys");

std::string_view DeviceTypeName(DeviceType type);
} // namespace Jettison::Render
