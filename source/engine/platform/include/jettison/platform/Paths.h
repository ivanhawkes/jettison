#pragma once

#include <filesystem>

namespace Jettison::Platform
{
// Where the engine reads and writes outside its asset tree, following the XDG base directory
// specification on Linux: user data (saves), configuration (settings, keybindings) and cache
// (shader caches, thumbnails), each under an application name. Filled in phase 2.
struct Paths
{
	std::filesystem::path userData;
	std::filesystem::path config;
	std::filesystem::path cache;
};

Paths ResolvePaths(const std::filesystem::path &applicationName);

// The running executable, resolved through /proc/self/exe.
std::filesystem::path ExecutablePath();
} // namespace Jettison::Platform
