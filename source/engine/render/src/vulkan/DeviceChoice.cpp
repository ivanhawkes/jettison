#include "vulkan/DeviceChoice.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <tuple>

namespace Jettison::Render
{
namespace
{
bool ContainsIgnoringCase(std::string_view text, std::string_view part)
{
	if (part.empty())
		return true;
	const auto lower = [](char c)
	{ return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
	return !std::ranges::search(text, part, {}, lower, lower).empty();
}

int TypeRank(DeviceType type)
{
	switch (type)
	{
		case DeviceType::Discrete: return 0;
		case DeviceType::Integrated: return 1;
		case DeviceType::Virtual: return 2;
		case DeviceType::Cpu: return 3;
		case DeviceType::Other: return 4;
	}
	return 4;
}

std::string Describe(const DeviceCandidate &candidate)
{
	const std::string_view display = !candidate.drivesDisplay   ? "display unknown"
	                                 : *candidate.drivesDisplay ? "a monitor connected"
	                                                            : "no monitor connected";
	return std::format("{} ({}, {})", candidate.name, DeviceTypeName(candidate.type), display);
}
} // namespace

std::string_view DeviceTypeName(DeviceType type)
{
	switch (type)
	{
		case DeviceType::Discrete: return "discrete";
		case DeviceType::Integrated: return "integrated";
		case DeviceType::Virtual: return "virtual";
		case DeviceType::Cpu: return "CPU";
		case DeviceType::Other: return "other";
	}
	return "other";
}

DeviceChoice ChooseDevice(std::span<const DeviceCandidate> candidates, std::string_view requested)
{
	std::optional<std::size_t> best;
	const auto key = [&candidates](std::size_t index)
	{
		const DeviceCandidate &candidate = candidates[index];
		return std::tuple{candidate.drivesDisplay == true ? 0 : 1, TypeRank(candidate.type), index};
	};
	for (std::size_t index = 0; index < candidates.size(); ++index)
	{
		const DeviceCandidate &candidate = candidates[index];
		if (!candidate.unsuitable.empty())
			continue;
		if (!requested.empty())
		{
			if (ContainsIgnoringCase(candidate.name, requested))
				return {index, std::format("{}, as asked for ('{}')", Describe(candidate), requested)};
			continue;
		}
		if (!best || key(index) < key(*best))
			best = index;
	}

	if (!requested.empty())
	{
		std::string listed;
		for (const DeviceCandidate &candidate : candidates)
			listed += std::format(
			    "; {}{}", candidate.name,
			    candidate.unsuitable.empty() ? std::string{} : ": " + candidate.unsuitable);
		return {std::nullopt, std::format("no usable GPU's name has '{}'{}", requested, listed)};
	}
	if (!best)
	{
		std::string listed;
		for (const DeviceCandidate &candidate : candidates)
			listed += std::format("; {}: {}", candidate.name, candidate.unsuitable);
		return {
		    std::nullopt, candidates.empty() ? std::string{"there is no GPU with Vulkan"}
		                                     : std::format("no GPU can be drawn with{}", listed)};
	}
	const DeviceCandidate &chosen = candidates[*best];
	const std::string why =
	    chosen.drivesDisplay == true ? "the monitor is on it"
	    : chosen.type == DeviceType::Discrete
	        ? "no usable GPU says it has the monitor, and it is the first discrete one"
	        : "it is the best of the usable devices";
	return {best, std::format("{}: {}", Describe(chosen), why)};
}

std::optional<bool> DrivesDisplay(
    std::int64_t major, std::int64_t minor, const std::filesystem::path &sysfs)
{
	std::error_code error;
	const std::filesystem::path node =
	    std::filesystem::canonical(sysfs / "dev" / "char" / std::format("{}:{}", major, minor), error);
	if (error || node.parent_path().filename() != "drm")
		return std::nullopt;
	// A card with no connectors at all, a compute card's, has no monitor either.
	const std::string card = node.filename().string();
	for (const auto &entry : std::filesystem::directory_iterator{node, error})
	{
		const std::string name = entry.path().filename().string();
		if (!name.starts_with(card + "-"))
			continue;
		std::ifstream file{entry.path() / "status"};
		std::string status;
		if (!(file >> status))
			continue;
		if (status == "connected")
			return true;
	}
	if (error)
		return std::nullopt;
	return false;
}
} // namespace Jettison::Render
