#pragma once

#include <filesystem>
#include <string>

namespace Jettison::Vfs
{
// One layer of the file system: a directory (or, from phase 11, a pack file) mapped under a
// virtual root. Later mounts shadow earlier ones path by path, which is how a mod overrides a
// base asset. Only the user mount is writable, and it is the last, so a file written is the file
// read back.
struct Mount
{
	std::string name;
	std::filesystem::path source;
	bool writable{false};
};
} // namespace Jettison::Vfs
