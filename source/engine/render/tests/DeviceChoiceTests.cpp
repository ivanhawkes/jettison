#include "vulkan/DeviceChoice.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

using namespace Jettison::Render;

namespace
{
const DeviceCandidate kSoftware{"llvmpipe (LLVM 21.1.8, 256 bits)", DeviceType::Cpu, {}, std::nullopt};

DeviceCandidate Gpu(std::string name, std::optional<bool> monitor, std::string unsuitable = {})
{
	return {std::move(name), DeviceType::Discrete, std::move(unsuitable), monitor};
}
} // namespace

TEST_CASE("Device choice: the GPU with the monitor, over one kept for compute listed before it")
{
	// Two NVIDIA cards, the second with the monitor: the first is for inference, and Vulkan may list
	// it first.
	const std::vector<DeviceCandidate> candidates{
	    Gpu("NVIDIA GeForce RTX 5070 Ti", false), Gpu("NVIDIA GeForce RTX 5060 Ti", true), kSoftware};
	const DeviceChoice choice = ChooseDevice(candidates);
	REQUIRE(choice.index);
	CHECK(*choice.index == 1);
	CHECK(choice.reason.find("the monitor is on it") != std::string::npos);
}

TEST_CASE("Device choice: under Xvfb the cards cannot present, and the CPU device is drawn with")
{
	const std::vector<DeviceCandidate> candidates{
	    Gpu("NVIDIA GeForce RTX 5060 Ti", true, "it cannot present to the window"),
	    Gpu("NVIDIA GeForce RTX 5070 Ti", false, "it cannot present to the window"), kSoftware};
	const DeviceChoice choice = ChooseDevice(candidates);
	REQUIRE(choice.index);
	CHECK(*choice.index == 2);
}

TEST_CASE(
    "Device choice: with no monitor to go by, a discrete GPU, then an integrated one, then the CPU")
{
	DeviceCandidate integrated{"Intel Graphics", DeviceType::Integrated, {}, std::nullopt};
	std::vector<DeviceCandidate> candidates{kSoftware, integrated, Gpu("Radeon", std::nullopt)};
	CHECK(ChooseDevice(candidates).index == 2u);
	candidates.pop_back();
	CHECK(ChooseDevice(candidates).index == 1u);
	// Two of a type keep Vulkan's order, which the device-select layer puts the display's first in.
	candidates = {Gpu("First", std::nullopt), Gpu("Second", std::nullopt)};
	CHECK(ChooseDevice(candidates).index == 0u);
	// A laptop's monitor on its integrated GPU: that one, over the discrete GPU beside it.
	integrated.drivesDisplay = true;
	candidates = {Gpu("GeForce", false), integrated};
	CHECK(ChooseDevice(candidates).index == 1u);
}

TEST_CASE("Device choice: a GPU asked for by name is taken if it can be used, and refused if not")
{
	const std::vector<DeviceCandidate> candidates{
	    Gpu("NVIDIA GeForce RTX 5070 Ti", false), Gpu("NVIDIA GeForce RTX 5060 Ti", true),
	    Gpu("Broken Card", true, "it is not Vulkan 1.3"), kSoftware};
	CHECK(ChooseDevice(candidates, "5070").index == 0u);
	CHECK(ChooseDevice(candidates, "LLVMPIPE").index == 3u);
	const DeviceChoice broken = ChooseDevice(candidates, "broken");
	CHECK_FALSE(broken.index);
	CHECK(broken.reason.find("Broken Card: it is not Vulkan 1.3") != std::string::npos);
	CHECK_FALSE(ChooseDevice(candidates, "Arc").index);

	CHECK_FALSE(ChooseDevice({}).index);
	const std::vector<DeviceCandidate> none{Gpu("Old", true, "it is not Vulkan 1.3")};
	const DeviceChoice nothing = ChooseDevice(none);
	CHECK_FALSE(nothing.index);
	CHECK(nothing.reason == "no GPU can be drawn with; Old: it is not Vulkan 1.3");
}

TEST_CASE("Device choice: a monitor is connected when one of the card's connectors says so")
{
	const auto sysfs =
	    std::filesystem::temp_directory_path() / ("jettison-device-tests-" + std::to_string(getpid()));
	std::error_code error;
	std::filesystem::remove_all(sysfs, error);
	const auto card = [&](const std::string &name, int minor, std::vector<std::string> statuses)
	{
		const auto directory = sysfs / "devices" / "pci0000:00" / "drm" / name;
		for (std::size_t index = 0; index < statuses.size(); ++index)
		{
			const auto connector = directory / (name + "-DP-" + std::to_string(index + 1));
			std::filesystem::create_directories(connector);
			std::ofstream{connector / "status"} << statuses[index] << '\n';
		}
		std::filesystem::create_directories(directory);
		std::filesystem::create_directories(sysfs / "dev" / "char");
		std::filesystem::create_directory_symlink(
		    directory, sysfs / "dev" / "char" / ("226:" + std::to_string(minor)));
	};
	card("card0", 0, {"disconnected", "disconnected"});
	card("card1", 1, {"disconnected", "connected"});
	card("card2", 2, {});

	CHECK(DrivesDisplay(226, 0, sysfs) == false);
	CHECK(DrivesDisplay(226, 1, sysfs) == true);
	// A compute card with no connectors has no monitor.
	CHECK(DrivesDisplay(226, 2, sysfs) == false);
	// A node sysfs does not know, or one that is not a DRM card, says nothing.
	CHECK_FALSE(DrivesDisplay(226, 9, sysfs));
	std::filesystem::create_directories(sysfs / "devices" / "input" / "event3");
	std::filesystem::create_directory_symlink(
	    sysfs / "devices" / "input" / "event3", sysfs / "dev" / "char" / "13:67");
	CHECK_FALSE(DrivesDisplay(13, 67, sysfs));
	std::filesystem::remove_all(sysfs);
}
