#include <jettison/profile/Profile.h>

#include <doctest/doctest.h>

TEST_CASE("Profile: compiled out, and the macros cost nothing")
{
	JETTISON_PROFILE_ZONE("test");
	JETTISON_PROFILE_PLOT("value", 1.0f);
	JETTISON_PROFILE_FRAME();
	CHECK_FALSE(Jettison::Profile::IsEnabled());
}
