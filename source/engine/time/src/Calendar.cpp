#include <jettison/time/Calendar.h>

#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>

#include <jettison/core/Assert.h>

namespace Jettison::Time
{
namespace
{
constexpr std::int64_t kSecondsPerHour = 3600;
constexpr std::int64_t kSecondsPerMinute = 60;
// 2^53: past it, a double no longer holds every whole second, so the moment a date names is no
// longer the one it was computed from. Two hundred and eighty million years of simulation time.
constexpr double kLargestExactSecond = 9007199254740992.0;

// Division rounding towards negative infinity, so the second before the epoch is the last second
// of the year before it rather than second -1 of the epoch's day 0. The divisors are positive.
std::int64_t FloorDivide(std::int64_t value, std::int64_t divisor)
{
	const std::int64_t quotient = value / divisor;
	return (value % divisor < 0) ? quotient - 1 : quotient;
}
std::int64_t FloorModulo(std::int64_t value, std::int64_t divisor)
{
	return value - FloorDivide(value, divisor) * divisor;
}

std::int32_t Narrow(std::int64_t value)
{
	JETTISON_ASSERT(
	    value >= std::numeric_limits<std::int32_t>::min() &&
	    value <= std::numeric_limits<std::int32_t>::max());
	return static_cast<std::int32_t>(value);
}
} // namespace

Calendar::Calendar(std::int64_t secondsPerDay, std::int32_t daysPerYear, std::int32_t epochYear)
    : m_secondsPerDay{secondsPerDay}, m_daysPerYear{daysPerYear}, m_epochYear{epochYear}
{
	if (secondsPerDay <= 0)
		throw std::invalid_argument{
		    std::format("Calendar: a day of {} seconds; it must be positive", secondsPerDay)};
	if (daysPerYear <= 0)
		throw std::invalid_argument{
		    std::format("Calendar: a year of {} days; it must be positive", daysPerYear)};
}

Date Calendar::FromSimSeconds(double seconds) const
{
	JETTISON_ASSERT(std::abs(seconds) <= kLargestExactSecond);
	const auto whole = static_cast<std::int64_t>(std::floor(seconds));
	const std::int64_t days = FloorDivide(whole, m_secondsPerDay);
	const std::int64_t intoDay = FloorModulo(whole, m_secondsPerDay);
	Date date;
	date.year = Narrow(m_epochYear + FloorDivide(days, m_daysPerYear));
	date.day = Narrow(FloorModulo(days, m_daysPerYear));
	date.hour = Narrow(intoDay / kSecondsPerHour);
	date.minute = Narrow(intoDay % kSecondsPerHour / kSecondsPerMinute);
	date.second = Narrow(intoDay % kSecondsPerMinute);
	return date;
}

double Calendar::ToSimSeconds(const Date &date) const
{
	const std::int64_t days =
	    (std::int64_t{date.year} - m_epochYear) * m_daysPerYear + std::int64_t{date.day};
	const std::int64_t seconds = days * m_secondsPerDay + std::int64_t{date.hour} * kSecondsPerHour +
	                             std::int64_t{date.minute} * kSecondsPerMinute +
	                             std::int64_t{date.second};
	return static_cast<double>(seconds);
}

std::string Calendar::Format(const Date &date) const
{
	return std::format(
	    "{:04}-{:03} {:02}:{:02}:{:02}", date.year, date.day + 1, date.hour, date.minute, date.second);
}
} // namespace Jettison::Time
