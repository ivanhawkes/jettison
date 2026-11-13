#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/vfs/FileSystem.h>

namespace Jettison::Vfs::Detail
{
// What a mount reads from: a directory or a pack. Paths reaching it are valid virtual paths; the
// calls are const and safe from any thread.
class Source
{
  public:
	virtual ~Source() = default;

	// The file's time and size, with no mount set.
	virtual std::optional<FileInfo> Find(std::string_view virtualPath) const = 0;
	virtual std::optional<std::vector<std::byte>> Read(std::string_view virtualPath) const = 0;
	// Appends every file under the directory (a valid virtual path, or empty for the root).
	virtual void List(std::string_view virtualDirectory, std::vector<std::string> &paths) const = 0;
};

std::shared_ptr<const Source> MakeDirectorySource(std::filesystem::path root);
// Throws std::runtime_error naming the pack and what is wrong with it.
std::shared_ptr<const Source> MakePackSource(const std::filesystem::path &pack);
} // namespace Jettison::Vfs::Detail
