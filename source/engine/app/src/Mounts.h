#pragma once

#include <filesystem>
#include <string_view>

namespace Jettison::Vfs
{
class FileSystem;
}

namespace Jettison::App
{
// Where the user's mods are, under the user's data directory: each a directory, or a pack of one
// (vfs/Pack.h), named by its file's name.
inline constexpr std::string_view kModDirectory = "mods";

// The shell's mounts, bottom to top: the assets (a directory or a pack) as "base"; each mod in the
// user's data directory, in the order of their names' bytes, as "mod:<name>"; the user's data
// directory itself, writable, as "user", where saves go. The order is the mods' names so it is the
// same on every machine and every run, and a player orders their mods by naming them; it is logged,
// with what each mount is. A mod that cannot be mounted (a damaged pack) is reported and left out,
// and anything else in the mods directory is reported and ignored, so one bad mod does not stop the
// game. Throws as Vfs::FileSystem::AddMount does for the assets or the user's directory.
//
// The user mount's root is the data directory, so its mods are under "mods/" in the virtual tree
// too; nothing reads there.
void MountAssets(
    Vfs::FileSystem &files, const std::filesystem::path &assets, const std::filesystem::path &userData);
} // namespace Jettison::App
