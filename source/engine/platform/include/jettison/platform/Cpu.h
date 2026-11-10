#pragma once

#include <optional>
#include <string>

namespace Jettison::Platform
{
// The start-up CPU check. The engine's baseline is x86-64 with AVX2 and the instructions that
// come with it (docs/roadmap.md, "Decided"), set by Jolt's public instruction-set flags: once an
// executable links Jolt, the compiler may emit them anywhere, static initialisers included.
//
// So the check that matters is not a call. Every executable that links platform runs a guard
// from .preinit_array, which the dynamic loader calls before any static initialiser: on a machine
// below the baseline it writes the message below to stderr and exits with status 1, instead of an
// illegal-instruction fault somewhere in the start-up of a library. Its translation unit is
// compiled for the plain x86-64 baseline, and the guard calls nothing that another, AVX2-compiled
// translation unit could have supplied the linker's copy of: no inline function, no template.
//
// The functions here are the same check as a value, for tests and for a diagnostic report.
struct CpuFeatures
{
	bool avx2{false};
	bool bmi1{false};
	bool lzcnt{false};
	bool popcnt{false};
	bool f16c{false};
};

// The features of the machine this runs on. AVX2 counts only when the operating system saves the
// AVX register state, as the compiler's runtime checks it.
CpuFeatures DetectCpuFeatures();

// A message naming what is missing, or nothing when the features meet the baseline.
std::optional<std::string> CheckCpuBaseline(const CpuFeatures &features);
// The same, for this machine.
std::optional<std::string> CheckCpuBaseline();
} // namespace Jettison::Platform
