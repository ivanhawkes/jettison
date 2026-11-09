#pragma once

#include <filesystem>
#include <optional>

namespace Jettison::Renderer
{
// What the executable was asked to run. argv is read once, here, so the app shell is handed a
// description of the run instead of a command line: flotsam, jetsam and test share the shell,
// and none of them has to know how the flags are spelled.
struct AppArguments
{
	// The headless render test: the six-stage sequence the shell hands to SmokeSequence.
	bool smokeTest{false};

	// Models, shaders, textures and scene graphs. Defaults to the tree the build stages beside
	// the executable; --assets points it somewhere else.
	std::filesystem::path assetDirectory;

	// The scene graph loaded at start-up, and an optional second file to write it back to.
	std::filesystem::path scenePath;
	std::filesystem::path saveScenePath;
};

// Resolves the defaults - the asset tree beside the executable, and <assets>/scene/main.yaml -
// then reads the flags. Returns nothing, having printed the usage line, when an argument is not
// understood; the caller exits with failure.
std::optional<AppArguments> ParseAppArguments(
    int argc, char **argv, const std::filesystem::path &assetSubdirectory);
} // namespace Jettison::Renderer
