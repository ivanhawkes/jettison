#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <jettison/vfs/Mount.h>

namespace Jettison::Vfs
{
// Ordered mounts (base, DLC, mods, user) and lookup through the stack. Paths are virtual, forward
// slashes, relative to the mount root: "models/Fox.glb". Reading goes through here so pack files
// (phase 11) are invisible to callers. Filled in phase 3.
class FileSystem
{
  public:
	void AddMount(Mount mount);
	const std::vector<Mount> &Mounts() const
	{
		return m_mounts;
	}

	bool Exists(const std::string &virtualPath) const;
	// The bytes of the topmost file at the path, or nothing.
	std::optional<std::vector<std::byte>> Read(const std::string &virtualPath) const;
	// Writes to the writable mount; throws if there is none.
	void Write(const std::string &virtualPath, std::span<const std::byte> bytes) const;
	// Every virtual path under a directory, across all mounts, each once.
	std::vector<std::string> Enumerate(const std::string &virtualDirectory) const;
	// Where a virtual path resolves on disk, for the loaders that still take a filesystem path.
	std::optional<std::filesystem::path> Resolve(const std::string &virtualPath) const;

  private:
	std::vector<Mount> m_mounts;
};
} // namespace Jettison::Vfs
