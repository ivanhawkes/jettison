#pragma once

#include <optional>
#include <string>

namespace Jettison::Platform
{
// The start-up CPU check. The engine's baseline is x86-64 with AVX2 (docs/roadmap.md, "Decided"),
// set by Jolt's public instruction-set flags. Returns a message naming what is missing, or
// nothing when the machine meets the baseline. It is the one function in the engine that has to be
// compiled without those flags and called before any other engine code, so that a machine without
// AVX2 gets the message and not an illegal-instruction fault. Due no later than phase 7.
std::optional<std::string> CheckCpuBaseline();
} // namespace Jettison::Platform
