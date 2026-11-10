#include <jettison/time/Calendar.h>

#include <doctest/doctest.h>

#include <cstdint>
#include <stdexcept>

using namespace Jettison::Time;

namespace
{
constexpr std::int64_t kEarthDay = 86400;

double At(std::int64_t days, std::int64_t hour, std::int64_t minute, std::int64_t second)
{
	return static_cast<double>(days * kEarthDay + hour * 3600 + minute * 60 + second);
}
} // namespace

TEST_CASE("Calendar: simulation time 0 is the first second of the epoch year")
{
	const Calendar calendar{kEarthDay, 365, 3300};
	const Date date = calendar.FromSimSeconds(0.0);
	CHECK(date == Date{3300, 0, 0, 0, 0});
	CHECK(calendar.Format(date) == "3300-001 00:00:00");
}

TEST_CASE("Calendar: a moment's date, formatted as an ISO 8601 ordinal date")
{
	const Calendar calendar{kEarthDay, 365, 3300};
	const Date date = calendar.FromSimSeconds(At(365 + 42, 14, 5, 9));
	CHECK(date == Date{3301, 42, 14, 5, 9});
	CHECK(calendar.Format(date) == "3301-043 14:05:09");
}

TEST_CASE("Calendar: a moment is in the second it has started, not the nearest one")
{
	const Calendar calendar{kEarthDay, 365};
	CHECK(calendar.FromSimSeconds(59.999) == Date{0, 0, 0, 0, 59});
	CHECK(calendar.FromSimSeconds(At(1, 0, 0, 0) - 0.001) == Date{0, 0, 23, 59, 59});
	CHECK(calendar.FromSimSeconds(At(365, 0, 0, 0)) == Date{1, 0, 0, 0, 0});
}

TEST_CASE("Calendar: moments before simulation time 0 are dates before the epoch")
{
	const Calendar calendar{kEarthDay, 365, 3300};
	CHECK(calendar.FromSimSeconds(-1.0) == Date{3299, 364, 23, 59, 59});
	CHECK(calendar.FromSimSeconds(-0.5) == Date{3299, 364, 23, 59, 59});
	CHECK(calendar.FromSimSeconds(-At(365, 0, 0, 0)) == Date{3299, 0, 0, 0, 0});
}

TEST_CASE("Calendar: a day that is not a whole number of hours ends with a short hour")
{
	// A sol, to the second: 24 hours, 39 minutes and 35 seconds.
	const Calendar calendar{88775, 668};
	CHECK(calendar.FromSimSeconds(88774.0) == Date{0, 0, 24, 39, 34});
	CHECK(calendar.FromSimSeconds(88775.0) == Date{0, 1, 0, 0, 0});
}

TEST_CASE("Calendar: a date round-trips through simulation time")
{
	const Calendar calendar{kEarthDay, 365, 3300};
	for (const double seconds :
	     {0.0, 1.0, -1.0, At(42, 14, 5, 9), At(-400, 3, 2, 1), At(365 * 1000 + 364, 23, 59, 59)})
	{
		CAPTURE(seconds);
		CHECK(calendar.ToSimSeconds(calendar.FromSimSeconds(seconds)) == seconds);
	}
}

TEST_CASE("Calendar: fields out of range carry, so a round trip normalises")
{
	const Calendar calendar{kEarthDay, 365, 3300};
	const Date overflowing{3300, 364, 25, 0, 0};
	CHECK(calendar.FromSimSeconds(calendar.ToSimSeconds(overflowing)) == Date{3301, 0, 1, 0, 0});
}

TEST_CASE("Calendar: a shape that is not positive is refused")
{
	CHECK_THROWS_AS(Calendar(0, 365), std::invalid_argument);
	CHECK_THROWS_AS(Calendar(-86400, 365), std::invalid_argument);
	CHECK_THROWS_AS(Calendar(86400, 0), std::invalid_argument);
	const Calendar calendar{kEarthDay, 365, 3300};
	CHECK(calendar.SecondsPerDay() == kEarthDay);
	CHECK(calendar.DaysPerYear() == 365);
	CHECK(calendar.EpochYear() == 3300);
}
