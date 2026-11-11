#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

namespace Jettison::Core
{
// What a file being written is called until it is complete: beside the file, under a name with a
// colon in it, which no virtual path can have (Vfs::FileSystem), so nothing reads it as content.
inline constexpr std::string_view kPartialSuffix = ":partial";

// Writes a whole file, replacing any file there, so that a reader, a crash or a power cut finds
// either the old file or the new one and never part of either: the bytes go to the partial file
// beside it, which is flushed to the disk (fsync), renamed over the file, and the directory then
// flushed too, since the rename is an entry in the directory and is not on the disk until the
// directory is. A save written and reported as saved is still there after the power goes.
//
// Creates the file's directory if it is missing. Throws std::runtime_error naming the path and
// what failed, with the partial file removed and any old file as it was.
void WriteFileDurably(const std::filesystem::path &path, std::span<const std::byte> bytes);
// The same for text.
void WriteFileDurably(const std::filesystem::path &path, std::string_view text);
} // namespace Jettison::Core
