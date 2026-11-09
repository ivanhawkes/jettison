#pragma once

#include <filesystem>

namespace Jettison::Renderer
{
int RunRenderApplication(
    int argc, char **argv, const char *windowTitle, const std::filesystem::path &assetSubdirectory);
}