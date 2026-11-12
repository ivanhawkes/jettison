#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Jettison::App
{
// What the executable was asked to run. argv is read once, here, so the app shell is handed a
// description of the run instead of a command line: flotsam, jetsam and render_test share the
// shell, and none of them has to know how the flags are spelled.
struct AppArguments
{
	// The headless render test: the sequence of stages the shell hands to SmokeSequence.
	bool smokeTest{false};

	// Models, shaders, textures and scene graphs. Defaults to the tree the build stages beside
	// the executable; --assets points it somewhere else.
	std::filesystem::path assetDirectory;

	// The scene graph loaded at start-up, and an optional second file to write it back to.
	std::filesystem::path scenePath;
	std::filesystem::path saveScenePath;
	// A star system file, by virtual path (systems/halcyon.yaml), in place of a scene: the scene is
	// the one the system scene builds from it alone. Empty when the run loads a scene.
	std::string systemPath;

	// The galaxy whose systems a jump goes between, by virtual path. Without one named, the shell
	// reads systems/galaxy.yaml if the assets have it, and runs with no jumps if they do not; a
	// galaxy named and missing fails the start-up.
	std::optional<std::string> galaxyPath;

	// A measurement of the frame: this many frames timed after a warm-up, the GPU's time in each pass
	// and the frame's counts printed, and the window closed. None when zero.
	std::uint32_t benchmarkFrames{0};
	// The window's size, in place of the default; the smoke test's own wins over it.
	std::optional<std::uint32_t> windowWidth;
	std::optional<std::uint32_t> windowHeight;
	// The opaque depth prepass, which the renderer runs without when not asked for.
	bool depthPrepass{false};
	// Part of the name of the GPU to draw with, in place of the one the renderer chooses (the one
	// with the monitor on it, then a discrete one): "5060", "llvmpipe". Start-up fails, listing the
	// GPUs, when no usable one has it.
	std::string gpu;

	// Sound played to miniaudio's null backend, in place of the machine's device: everything sounds
	// as it would, at the pace it would, and nothing is heard or touches the sound system. The
	// headless run always plays so.
	bool noSound{false};
	// Where the user's files go, in place of the XDG directories: settings under <dir>/config, saves
	// under <dir>/data. The headless run with none named uses a directory of its own it removes as it
	// ends, so a test neither reads nor writes the settings and saves of whoever runs it.
	std::optional<std::filesystem::path> userDirectory;
};

// Resolves the defaults - the asset tree beside the executable, and <assets>/scene/main.yaml unless
// a system is named - then reads the flags. --scene and --system together are not understood. Returns
// nothing, having printed the usage line, when an argument is not understood; the caller exits with
// failure.
std::optional<AppArguments> ParseAppArguments(
    int argc, char **argv, const std::filesystem::path &assetSubdirectory);
} // namespace Jettison::App
