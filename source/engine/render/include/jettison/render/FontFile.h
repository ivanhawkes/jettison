#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace Jettison::Render
{
// The checks of a TrueType or OpenType file made before FreeType reads it, for each loader of a
// face (the dev tools' and the game UI's): it is at most 32 MiB, it begins with an sfnt version
// (TrueType's, "OTTO" or "true") or is a collection ("ttcf") of such fonts, and each font's table
// directory, and every table it lists, lies within the file. Throws std::runtime_error naming the
// file for one that breaks any of it.
void CheckFontFile(std::span<const std::byte> bytes, std::string_view name);
} // namespace Jettison::Render
