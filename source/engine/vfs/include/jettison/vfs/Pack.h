#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Jettison::Vfs
{
// A pack file: a directory's files in one file, which FileSystem mounts as it does the directory,
// for shipping. Each file is kept on its own, stored as it is or compressed as one zstd frame with
// its checksum, so one is read without reading the rest, and a damaged one is caught as it is
// read. A file is compressed only where that pays: the sample assets are 87 MB, 75 MB of them
// textures already zstd-supercompressed in their KTX 2 files, and zstd at level 1 saves under
// 0.1 % on those and on the models, about half on the text (measured 2026-11-13).
//
// The layout, little-endian:
//   header   "JETPACK" and a zero byte; u32 layout (1); u32 file count; u64 index offset; u64 index
//            size
//   data     every file's bytes, as stored, in the index's order
//   index    for each file: u32 path length, the path (a valid virtual path), u8 method (0 stored,
//            1 zstd), u64 offset of its bytes, u64 their size as stored, u64 its size
// The index is sorted by path, each once, and every file's bytes lie between the header and the
// index; a pack that breaks any of this is refused as it is mounted.
inline constexpr std::uint32_t kPackLayout = 1;

struct PackOptions
{
	// zstd's level for a file that compresses (1 to 22). A file is tried at level 1 first, and
	// stored as it is unless that saves a thirty-second of it, so the level is spent on the files
	// it helps: at 19, the sample assets pack in under a second, against 22 CPU-seconds for the
	// whole tree at 19.
	int level{19};
	// The directories under the root to pack, as virtual paths, or every file under it when empty:
	// a build's staged assets share their directory with the build system's own files.
	std::vector<std::string> directories;
};

struct PackSummary
{
	std::size_t files{0};
	std::size_t compressed{0};
	std::uintmax_t bytes{0};
	std::uintmax_t packedBytes{0};
	// Files under the directory whose names no virtual path could have, so nothing could read
	// them, and left out.
	std::vector<std::string> skipped;
};

// Packs every regular file under the directory, or under the directories the options name,
// recursively, its path under the directory its virtual path, so the pack mounted reads as the
// directory does. The same files give the same bytes. The pack is written beside the output, its
// directory made if it is missing, and renamed over it once complete, so a reader never finds half
// of one. Throws
// std::invalid_argument for a level out of range or a directory to pack that is not a virtual
// path, and std::runtime_error for a directory that cannot be read or a pack that cannot be
// written.
PackSummary WritePack(
    const std::filesystem::path &directory, const std::filesystem::path &pack,
    PackOptions options = {});
} // namespace Jettison::Vfs
