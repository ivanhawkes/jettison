#include <jettison/platform/Paths.h>

#include <cstdlib>
#include <format>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace Jettison::Platform
{
namespace
{
// The specification: a variable that is unset, empty or not an absolute path is ignored, and the
// default under HOME is used instead.
std::optional<std::filesystem::path> AbsoluteVariable(
    const EnvironmentLookup &environment, const char *name)
{
	const std::optional<std::string> value = environment(name);
	if (!value || value->empty())
		return std::nullopt;
	std::filesystem::path path{*value};
	if (!path.is_absolute())
		return std::nullopt;
	return path;
}

std::filesystem::path BaseDirectory(
    const EnvironmentLookup &environment, const char *variable, const std::filesystem::path &underHome)
{
	if (std::optional<std::filesystem::path> path = AbsoluteVariable(environment, variable))
		return *std::move(path);
	const std::optional<std::filesystem::path> home = AbsoluteVariable(environment, "HOME");
	if (!home)
		throw std::runtime_error{
		    std::format("Platform: {} is unset or relative, and so is HOME", variable)};
	return *home / underHome;
}

void CheckApplicationName(const std::filesystem::path &name)
{
	const bool oneComponent = !name.empty() && std::distance(name.begin(), name.end()) == 1;
	if (!oneComponent || name.is_absolute() || name == "." || name == "..")
		throw std::invalid_argument{std::format(
		    "Platform: '{}' is not an application name: it must be one path component", name.string())};
}
} // namespace

Paths ResolvePaths(const std::filesystem::path &applicationName)
{
	return ResolvePaths(
	    applicationName,
	    [](const char *name) -> std::optional<std::string>
	    {
		    // Read at start-up, before any thread that could call setenv exists.
		    const char *value = std::getenv(name); // NOLINT(concurrency-mt-unsafe)
		    if (value == nullptr)
			    return std::nullopt;
		    return std::string{value};
	    });
}

Paths ResolvePaths(const std::filesystem::path &applicationName, const EnvironmentLookup &environment)
{
	CheckApplicationName(applicationName);
	return Paths{
	    .userData = BaseDirectory(environment, "XDG_DATA_HOME", ".local/share") / applicationName,
	    .config = BaseDirectory(environment, "XDG_CONFIG_HOME", ".config") / applicationName,
	    .state = BaseDirectory(environment, "XDG_STATE_HOME", ".local/state") / applicationName,
	    .cache = BaseDirectory(environment, "XDG_CACHE_HOME", ".cache") / applicationName,
	};
}

std::filesystem::path ExecutablePath()
{
	return std::filesystem::read_symlink("/proc/self/exe");
}
} // namespace Jettison::Platform
