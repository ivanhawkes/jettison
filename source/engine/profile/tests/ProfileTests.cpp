#include <jettison/profile/Profile.h>

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

using namespace Jettison::Profile;

namespace
{
int ProfiledFunction()
{
	JETTISON_PROFILE_FUNCTION();
	return 1;
}
} // namespace

TEST_CASE("Profile: the build says whether the client is compiled in")
{
#ifdef TRACY_ENABLE
	CHECK(IsEnabled());
#else
	CHECK_FALSE(IsEnabled());
	CHECK_FALSE(IsConnected());
#endif
}

TEST_CASE("Profile: zones, plots and frame marks compile in either build")
{
	{
		JETTISON_PROFILE_ZONE("test");
		CHECK(ProfiledFunction() == 1);
	}
	// Each of these is ambiguous between Tracy's three plot overloads without PlotValue.
	const int count = 3;
	const std::size_t bytes = 4096;
	JETTISON_PROFILE_PLOT("count", count);
	JETTISON_PROFILE_PLOT("bytes", bytes);
	JETTISON_PROFILE_PLOT("ratio", 0.5f);
	JETTISON_PROFILE_PLOT("seconds", 0.25);
	JETTISON_PROFILE_FRAME();
}

TEST_CASE("Profile: integers plot as int64_t, floating point as itself")
{
	static_assert(std::is_same_v<decltype(PlotValue(1)), std::int64_t>);
	static_assert(std::is_same_v<decltype(PlotValue(std::size_t{1})), std::int64_t>);
	static_assert(std::is_same_v<decltype(PlotValue(std::uint8_t{1})), std::int64_t>);
	static_assert(std::is_same_v<decltype(PlotValue(1.0f)), float>);
	static_assert(std::is_same_v<decltype(PlotValue(1.0)), double>);
	CHECK(PlotValue(std::size_t{4096}) == 4096);
}
