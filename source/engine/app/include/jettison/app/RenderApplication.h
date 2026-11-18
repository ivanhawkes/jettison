#pragma once

#include <filesystem>

namespace Jettison::App
{
class Game;

// The shell (docs/architecture.md, "App shell") run from a program's main: its arguments parsed, the
// engine's assets and the game's content found under the program's directory, and the game composed
// into it. No game and no content is the engine with none. The game outlives the run. Returns the
// program's exit status.
int RunRenderApplication(
    int argc, char **argv, const char *windowTitle, const std::filesystem::path &assetSubdirectory,
    Game *game = nullptr, const std::filesystem::path &contentSubdirectory = {});
} // namespace Jettison::App