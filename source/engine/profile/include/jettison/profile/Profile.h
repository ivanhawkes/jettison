#pragma once

#include <tracy/Tracy.hpp>

#include <concepts>
#include <cstdint>

namespace Jettison::Profile
{
// Tracy, behind macros that expand to Tracy's when JETTISON_PROFILE is on and to nothing when it
// is off. The client is on demand and listens on localhost only, so a build with profiling
// compiled in costs nothing until a profiler connects. GPU zones for Vulkan are the renderer's, one
// per declared pass, through Tracy's own TracyVulkan.hpp (render/vulkan/GpuProfiler.h).

// Whether this build has the Tracy client compiled in (JETTISON_PROFILE).
bool IsEnabled();
// Whether a profiler is connected now. Always false when profiling is compiled out.
bool IsConnected();

// Tracy has a plot overload for int64_t, float and double, so an int or a size_t is ambiguous
// between the three. Every integer plots as int64_t.
template <std::integral T> constexpr std::int64_t PlotValue(T value)
{
	return static_cast<std::int64_t>(value);
}
constexpr float PlotValue(float value)
{
	return value;
}
constexpr double PlotValue(double value)
{
	return value;
}
} // namespace Jettison::Profile

// Names are string literals: Tracy keeps the pointer, not a copy, and identifies a zone or a plot
// by it.

// A named scope on the profiler's timeline, from here to the end of the enclosing block.
#define JETTISON_PROFILE_ZONE(name) ZoneScopedN(name)
// A zone named after the enclosing function.
#define JETTISON_PROFILE_FUNCTION() ZoneScoped
// The end of one frame.
#define JETTISON_PROFILE_FRAME() FrameMark

// A value plotted against time. Compiled out, the value is named in an unevaluated operand, so a
// variable that only a plot reads does not become an unused-variable warning.
#ifdef TRACY_ENABLE
#define JETTISON_PROFILE_PLOT(name, value) TracyPlot(name, ::Jettison::Profile::PlotValue(value))
#else
#define JETTISON_PROFILE_PLOT(name, value)                                                             \
	static_cast<void>(sizeof(name), sizeof(::Jettison::Profile::PlotValue(value)))
#endif
