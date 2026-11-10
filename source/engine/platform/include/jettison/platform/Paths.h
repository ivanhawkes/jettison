#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace Jettison::Platform
{
// Where the engine reads and writes outside its asset tree, following the XDG base directory
// specification (0.8): user data (saves), configuration (settings, keybindings), state (logs,
// console history) and cache (shader caches, thumbnails), each under an application name.
//
// Resolving touches nothing on disk. The specification has a writer create a directory that does
// not exist yet, with mode 0700, when it first writes there, so that is the writer's job.
struct Paths
{
	std::filesystem::path userData;
	std::filesystem::path config;
	std::filesystem::path state;
	std::filesystem::path cache;
};

// An environment variable's value, or nothing when it is unset.
using EnvironmentLookup = std::function<std::optional<std::string>(const char *name)>;

// The paths for applicationName, from the process environment. The name is one relative path
// component ("jettison", not "games/jettison"); anything else throws std::invalid_argument.
// Throws std::runtime_error naming the variable when HOME is needed and is not an absolute path.
Paths ResolvePaths(const std::filesystem::path &applicationName);
// The same, from the variables environment returns, for tests and for a launcher that passes its
// own.
Paths ResolvePaths(const std::filesystem::path &applicationName, const EnvironmentLookup &environment);

// The running executable, resolved through /proc/self/exe. Throws std::filesystem::filesystem_error
// when /proc is not mounted.
std::filesystem::path ExecutablePath();
} // namespace Jettison::Platform
