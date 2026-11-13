#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/vfs/Mount.h>

namespace Jettison::Vfs
{
namespace Detail
{
class Source;
}

// Which mount holds a file, when it was written and its size: enough to tell that the file a path
// names has changed, or that another mount's now shadows it (the resource manager's hot reload). A
// file in a pack has the pack's time, and a pack is read as it was when it was mounted.
struct FileInfo
{
	std::size_t mount{0};
	std::filesystem::file_time_type time{};
	std::uintmax_t size{0};

	bool operator==(const FileInfo &) const = default;
};

// One mount's file at a path, for a reader that layers them (Sim::DefinitionRegistry).
struct Layer
{
	std::size_t mount{0};
	std::vector<std::byte> bytes;
};

// Ordered mounts (base, DLC, mods, user) and lookup through the stack. Reading goes through here,
// so whether a mount is a directory or a pack file is invisible to callers.
//
// A virtual path is relative to every mount's root, with forward slashes: "models/Fox.glb". Its
// segments are not empty, ".", or "..", and it holds no backslash, colon or control character, so
// data cannot name a file outside a mount, and a path a mod uses means the same on every platform.
// A path that breaks these rules is an error in the data that names it, and every call below
// throws std::invalid_argument for one rather than answering as if the file were absent. Paths
// are case-sensitive, as the disk is. A symbolic link inside a mounted directory is followed.
//
// Mounts are added at start-up, before any reader runs; after that the const calls are safe from
// any thread, the resource manager's I/O worker included.
class FileSystem
{
  public:
	// A source that is a regular file is mounted as a pack. Throws std::invalid_argument for a
	// mount with no name, a name already mounted, a second writable mount or a mount after the
	// writable one, and std::runtime_error for a read-only source that is neither a directory nor a
	// pack, naming what is wrong with it. A writable source is a directory, created if it does not
	// exist yet.
	void AddMount(Mount mount);
	const std::vector<Mount> &Mounts() const
	{
		return m_mounts;
	}

	// Whether the path names a regular file in any mount.
	bool Exists(std::string_view virtualPath) const;
	// The topmost file at the path, or nothing. Its mount is an index into Mounts().
	std::optional<FileInfo> Find(std::string_view virtualPath) const;
	// The bytes of the topmost file at the path, or nothing. Throws std::runtime_error if the file
	// is there and cannot be read, or is damaged in its pack.
	std::optional<std::vector<std::byte>> Read(std::string_view virtualPath) const;
	// The file at the path in every mount that has one, the bottom mount's first. Throws as Read
	// does.
	std::vector<Layer> ReadLayers(std::string_view virtualPath) const;
	// Writes to the writable mount, replacing any file there whole, through Core::WriteFileDurably:
	// the bytes go to a file beside it that no virtual path names, flushed to the disk and renamed
	// over it, and the directory flushed, so a reader, a crash or a power cut never finds half a
	// save, and a save reported written is on the disk. Throws std::logic_error if there is no
	// writable mount, and std::runtime_error if the write fails.
	void Write(std::string_view virtualPath, std::span<const std::byte> bytes) const;
	// Every file under a directory, recursively, across all mounts, each once, sorted. The
	// directory is a virtual path, or empty for the root. A file whose name is not a valid
	// virtual path is not listed, since nothing could name it.
	std::vector<std::string> Enumerate(std::string_view virtualDirectory) const;

	// Whether text follows the rules above.
	static bool IsValidPath(std::string_view virtualPath);

  private:
	std::vector<Mount> m_mounts;
	// One a mount, in the same order; shared, so a file system copied reads the same open packs.
	std::vector<std::shared_ptr<const Detail::Source>> m_sources;
};
} // namespace Jettison::Vfs
