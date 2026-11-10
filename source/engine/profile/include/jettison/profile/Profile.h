#pragma once

namespace Jettison::Profile
{
// Tracy, behind macros that compile to nothing when JETTISON_PROFILE is off. Phase 2 makes the
// zone, frame-mark and plot macros expand to Tracy's when it is on, and adds GPU zones for
// Vulkan. Until then every build is the compiled-out build, and IsEnabled() says so.
bool IsEnabled();
} // namespace Jettison::Profile

// A named scope on the profiler's timeline.
#define JETTISON_PROFILE_ZONE(name) static_cast<void>(0)
// The end of one frame.
#define JETTISON_PROFILE_FRAME() static_cast<void>(0)
// A value plotted against time.
#define JETTISON_PROFILE_PLOT(name, value) static_cast<void>(0)
