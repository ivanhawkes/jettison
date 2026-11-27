#pragma once

#include <filesystem>
#include <memory>

#include <jettison/host/Session.h>

namespace Jettison::App
{
class Game;

// The shell (docs/app-shell.md) run from a program's main: its arguments parsed, the engine's assets
// and the game's content found under the program's directory, the game's world started through a
// Host::Session, and its presentation composed over it. No world is the shell's own, physics alone;
// no game and no content is the engine with none. The game outlives the run. Returns the program's
// exit status.
int RunRenderApplication(
    int argc, char **argv, const char *windowTitle, const std::filesystem::path &assetSubdirectory,
    std::unique_ptr<Host::Game> world = {}, Game *game = nullptr,
    const std::filesystem::path &contentSubdirectory = {});
} // namespace Jettison::App