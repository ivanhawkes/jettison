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

// What a durable write gives the file unless told otherwise: 0644, its owner's to write, everyone's to
// read.
inline constexpr std::filesystem::perms kFilePermissions =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
    std::filesystem::perms::group_read | std::filesystem::perms::others_read;

// Writes a whole file, replacing any file there, so that a reader, a crash or a power cut finds
// either the old file or the new one and never part of either: the bytes go to the partial file
// beside it, which is flushed to the disk (fsync), renamed over the file, and the directory then
// flushed too, since the rename is an entry in the directory and is not on the disk until the
// directory is. A save written and reported as saved is still there after the power goes.
//
// The file has the permissions given, under the process's umask, from the moment its partial file is
// made, which is made anew, so a secret is never open to others on its way to the disk; a file it
// replaces takes them. Creates the file's directory if it is missing, as CreateDirectoriesDurably
// does. Throws std::runtime_error naming the path and what failed, with the partial file removed and
// any old file as it was.
void WriteFileDurably(
    const std::filesystem::path &path, std::span<const std::byte> bytes,
    std::filesystem::perms permissions = kFilePermissions);

// The same for text.
void WriteFileDurably(
    const std::filesystem::path &path, std::string_view text,
    std::filesystem::perms permissions = kFilePermissions);

// Makes the directory and each one above it that is missing, flushing the parent of each as it is
// made: a new directory is an entry in its parent, and is lost in a power cut, with everything
// written under it, until that parent is on the disk. One that was already there is taken to be.
// True when it made any. Each it makes has the permissions given, under the process's umask, from the
// moment it is made: one made and then changed would keep what it was made with if the change were
// cut off. Throws std::runtime_error naming the directory that could not be made or flushed, or the
// path when something other than a directory is there.
bool CreateDirectoriesDurably(
    const std::filesystem::path &path,
    std::filesystem::perms permissions = std::filesystem::perms::all);
} // namespace Jettison::Core
