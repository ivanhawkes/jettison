#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Host
{
inline constexpr std::string_view kPackageDescriptionFile = "mod.yaml";
inline constexpr std::uint32_t kPackageFormatVersion = 1;

// What a package (a directory or a pack) says of itself in mod.yaml at its root.
struct PackageDescription
{
	std::string name;
	// Its own version, which a save records it by ("version" in the file is the format's).
	std::string release;
	// The oldest engine version the package runs on, "major.minor.patch"; empty for any.
	std::string engine;
	// The packages that must be mounted before it, by mount name; "requires" in the file.
	std::vector<std::string> required;
};

// Reflects PackageDescription. Safe to call any number of times, by any number of sessions.
void RegisterPackageTypes();

// The description at the root of a directory or pack, read through a mount of it alone, or none for
// a package with no mod.yaml. Throws for a source that cannot be mounted or a description that does
// not read. A field the file omits is left empty: whether one is required is the caller's policy.
std::optional<PackageDescription> DescribePackage(const std::filesystem::path &source);

// Whether the version is the required one or later. Throws std::invalid_argument for either that is
// not one to three numbers separated by dots.
bool EngineAtLeast(std::string_view engine, std::string_view required);
} // namespace Jettison::Host
