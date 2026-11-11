#pragma once

#include <string_view>

namespace Jettison::Core
{
// The engine's version, the project's in CMakeLists.txt ("0.1.0"). A save records the version that
// wrote it, so a report of one that does not load says which build made it; what a save can be
// read by is its format version, not this.
std::string_view EngineVersion();
} // namespace Jettison::Core
