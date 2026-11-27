#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::Host
{
// The format of WorldContent's bytes (docs/files-and-mods.md, "The server's world content").
inline constexpr std::uint32_t kWorldContentFormatVersion = 1;

// One package's world as a session runs on it: its mount name, its release, and a digest of the files
// under its world directories (kWorldDirectories), their paths and their bytes.
struct WorldPackage
{
	std::string mount;
	std::string release;
	std::uint64_t digest{};

	bool operator==(const WorldPackage &) const = default;
};

// The packages whose world a session runs on, in mount order: those holding world content, so a
// package of presentation alone, the client's own included, need not match. A server's game sends it
// to a client as it admits it, in its own welcome, and the client checks it against its own
// (ContentDifference) before it shows a world it may not be able to read.
struct WorldContent
{
	std::vector<WorldPackage> packages;

	bool operator==(const WorldContent &) const = default;
};

// Reflects WorldContent. Safe to call any number of times.
void RegisterWorldContentTypes();

// The world content of a file system's mounts, each read alone. Throws as Vfs::FileSystem does for a
// source that cannot be read, and as DescribePackage does for a description that does not read.
WorldContent DescribeWorldContent(const Vfs::FileSystem &files);

std::vector<std::byte> EncodeWorldContent(const WorldContent &content);

// Throws Serialise::FormatError (or its version error) for bytes that are not world content.
WorldContent DecodeWorldContent(std::span<const std::byte> bytes);

// Why the content held is not the content served, naming the first package that differs, for the
// client to show rather than a world it cannot read; none when they are the same.
std::optional<std::string> ContentDifference(const WorldContent &held, const WorldContent &served);
} // namespace Jettison::Host
