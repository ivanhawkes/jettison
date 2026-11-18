#pragma once

#include <filesystem>

namespace Jettison::App
{
class Game;

// The shell (docs/architecture.md, "App shell") run from a program's main: its arguments parsed, the
// assets found under the program's directory, and the game composed into it, or none, which is the
// engine with no game. The game outlives the run. Returns the program's exit status.
int RunRenderApplication(
    int argc, char **argv, const char *windowTitle, const std::filesystem::path &assetSubdirectory,
    Game *game = nullptr);
}