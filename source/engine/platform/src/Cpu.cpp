#include <jettison/platform/Cpu.h>

#include <unistd.h>

namespace Jettison::Platform
{
namespace
{
struct Requirement
{
	const char *name;
	bool CpuFeatures::*present;
};

// Jolt's instruction-set flags, in the order it adds them.
constexpr Requirement kBaseline[] = {
    {"AVX2", &CpuFeatures::avx2},   {"BMI1", &CpuFeatures::bmi1}, {"POPCNT", &CpuFeatures::popcnt},
    {"LZCNT", &CpuFeatures::lzcnt}, {"F16C", &CpuFeatures::f16c},
};

constexpr char kNeeds[] = "Jettison needs an x86-64 CPU with AVX2: Intel Haswell (2013), AMD Excavator "
                          "(2015) or later. This one is missing: ";

// Plain write(2): the guard runs before the C++ runtime is initialised, and may not call any
// function an AVX2-compiled translation unit could have supplied.
void WriteError(const char *text)
{
	const auto length = static_cast<size_t>(__builtin_strlen(text));
	[[maybe_unused]] const ssize_t written = write(STDERR_FILENO, text, length);
}

// Called by the dynamic loader from .preinit_array, with the arguments it passes there.
void GuardCpuBaseline(int /*argc*/, char ** /*argv*/, char ** /*environment*/)
{
	const CpuFeatures features = DetectCpuFeatures();
	bool missing = false;
	for (const Requirement &requirement : kBaseline)
	{
		if (features.*requirement.present)
			continue;
		WriteError(missing ? ", " : kNeeds);
		WriteError(requirement.name);
		missing = true;
	}
	if (missing)
	{
		WriteError(".\n");
		_exit(1);
	}
}
} // namespace

CpuFeatures DetectCpuFeatures()
{
	// Initialises the compiler runtime's CPU model now: its own constructor has not run yet when
	// the guard calls this.
	__builtin_cpu_init();
	// Aggregate initialisation, not the default constructor: that is an inline function, and the
	// guard may not call one.
	return CpuFeatures{
	    .avx2 = __builtin_cpu_supports("avx2") != 0,
	    .bmi1 = __builtin_cpu_supports("bmi") != 0,
	    .lzcnt = __builtin_cpu_supports("lzcnt") != 0,
	    .popcnt = __builtin_cpu_supports("popcnt") != 0,
	    .f16c = __builtin_cpu_supports("f16c") != 0,
	};
}

std::optional<std::string> CheckCpuBaseline(const CpuFeatures &features)
{
	std::string missing;
	for (const Requirement &requirement : kBaseline)
	{
		if (features.*requirement.present)
			continue;
		if (!missing.empty())
			missing += ", ";
		missing += requirement.name;
	}
	if (missing.empty())
		return std::nullopt;
	return std::string{kNeeds} + missing + ".";
}

std::optional<std::string> CheckCpuBaseline()
{
	return CheckCpuBaseline(DetectCpuFeatures());
}
} // namespace Jettison::Platform

// The loader calls each entry of the executable's .preinit_array before the initialisers of any
// shared library or static object. A shared library cannot have one, which is fine: the engine is
// static libraries linked into the game's executable. Named and external so platform's
// CMakeLists.txt can force the object into every executable with --require-defined. The
// one-declaration form of extern "C" is the one that gives a const variable external linkage; in
// a braced block it would stay internal.
extern "C" [[gnu::used, gnu::section(".preinit_array")]] void (*const JettisonCpuGuard)(
    int, char **, char **) = &Jettison::Platform::GuardCpuBaseline;
