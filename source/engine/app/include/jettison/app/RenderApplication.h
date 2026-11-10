#pragma once

#include <filesystem>

namespace Jettison::App
{
int RunRenderApplication(
    int argc, char **argv, const char *windowTitle, const std::filesystem::path &assetSubdirectory);
}