// Jolt's profiler hooks, which Jolt declares and leaves to its user under JPH_EXTERNAL_PROFILE. They
// are a library of their own that Jolt links (physics/CMakeLists.txt), not part of physics: an
// executable that calls Jolt without a physics world, as the dependency probe does, would otherwise
// never pull them out of physics' archive, and fail to link.
#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Core/Profiler.h>

#include <cstring>
#include <new>
#include <string_view>

#include <jettison/profile/Profile.h>

#ifdef JPH_EXTERNAL_PROFILE
// Jolt's zones on Tracy's timeline. Jolt names them with literals and function names, but a zone
// made here has no source location of its own to point at, so each is a transient zone, its
// location allocated when it starts: dearer than a static zone, and only while a profiler is
// connected. Compiled out, a measurement is nothing.
JPH::ExternalProfileMeasurement::ExternalProfileMeasurement(const char *inName, uint32 inColor)
{
#ifdef TRACY_ENABLE
	static_assert(sizeof(tracy::ScopedZone) <= sizeof(mUserData));
	static_assert(alignof(tracy::ScopedZone) <= alignof(ExternalProfileMeasurement));
	constexpr std::string_view kSource = "Jolt";
	new (mUserData) tracy::ScopedZone(
	    0, kSource.data(), kSource.size(), inName, std::strlen(inName), inName, std::strlen(inName),
	    inColor, -1, true);
#else
	(void)inName;
	(void)inColor;
	(void)mUserData;
#endif
}

JPH::ExternalProfileMeasurement::~ExternalProfileMeasurement()
{
#ifdef TRACY_ENABLE
	std::launder(reinterpret_cast<tracy::ScopedZone *>(mUserData))->~ScopedZone();
#endif
}
#endif
