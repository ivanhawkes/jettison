#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>

namespace Jettison::Core
{
// What a file being written is called until it is complete: a file in its directory named
// :partial.<process ID>.<mark>.<count>, the mark 16 hexadecimal digits a process draws once. Its
// length is bounded whatever the file's name, and its colon is a name no virtual path can have
// (Vfs::FileSystem), so nothing reads it as content. Each write has its own, since writers of the
// same file at once would otherwise write into one, and one could rename the other's half-written
// bytes into place. A process's first write into a directory removes the partials there, in exactly
// that form, of writers which no longer run, and never the file it writes.
inline constexpr std::string_view kPartialPrefix = ":partial";

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
// made, which is its own and made anew, so a secret is never open to others on its way to the disk; a
// file it replaces takes them. Creates the file's directory if it is missing, as
// CreateDirectoriesDurably does. Throws std::runtime_error naming the path and what failed, with the
// partial file removed and any old file as it was, except that the directory's flush fails after the
// rename: then the new file is in place, and the error says it was written but may not survive.
void WriteFileDurably(
    const std::filesystem::path &path, std::span<const std::byte> bytes,
    std::filesystem::perms permissions = kFilePermissions);

// The same for text.
void WriteFileDurably(
    const std::filesystem::path &path, std::string_view text,
    std::filesystem::perms permissions = kFilePermissions);

// What WriteNewFileDurably throws when a file is already there, so a writer that lost the race to
// make a file can tell it from a write that failed.
class FileExistsError : public std::runtime_error
{
  public:
	using std::runtime_error::runtime_error;
};

// WriteFileDurably for a file that must not be there yet: the same writes, but the rename that puts
// the file in place fails if any file is there (renameat2 with RENAME_NOREPLACE, Linux only), so
// that of two writers making the same file one leaves it whole and the other is refused, with no
// check between the two for a writer to come into. Throws FileExistsError naming the path when a file
// is there, and any other failure as WriteFileDurably does; either way the partial file is removed and
// a file already there is as it was. Needs a file system that supports the flag (ext4, xfs, btrfs and
// tmpfs do), or it throws for the file system.
void WriteNewFileDurably(
    const std::filesystem::path &path, std::span<const std::byte> bytes,
    std::filesystem::perms permissions = kFilePermissions);

// The same for text.
void WriteNewFileDurably(
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
