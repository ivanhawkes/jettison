#pragma once

#include <filesystem>
#include <string>

namespace Jettison::Vfs
{
// One layer of the file system: a directory or a pack file (Pack.h) mapped under a virtual root.
// Later mounts shadow earlier ones path by path, which is how a mod overrides a base asset. Only
// the user mount is writable, and it is the last, so a file written is the file read back; a pack
// is read-only.
struct Mount
{
	std::string name;
	std::filesystem::path source;
	bool writable{false};
};
} // namespace Jettison::Vfs
