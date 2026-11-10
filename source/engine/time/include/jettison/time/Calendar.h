#pragma once

#include <cstdint>
#include <string>

namespace Jettison::Time
{
// A moment as an in-game date: a year, the day within it, and the time of day.
struct Date
{
	std::int32_t year{0};
	std::int32_t day{0}; // Day of the year, from 0.
	std::int32_t hour{0};
	std::int32_t minute{0};
	std::int32_t second{0};

	friend bool operator==(const Date &, const Date &) = default;
};

// Simulation seconds as an in-game date. The calendar's shape (the length of a day, the days in a
// year, the year simulation time 0 falls in) is data the game supplies; the engine does the
// arithmetic. Hours, minutes and seconds are SI, so a day that is not a whole number of hours ends
// with a short hour. Months are not modelled: the date is a day of the year until a game needs
// more.
//
// A day is a whole number of seconds, so once a moment is floored to its second, its date is exact
// integer arithmetic: there is no rounding to put a moment at the end of one day in the next.
class Calendar
{
  public:
	// secondsPerDay and daysPerYear are positive; anything else throws std::invalid_argument,
	// because they come from the game's data.
	Calendar(std::int64_t secondsPerDay, std::int32_t daysPerYear, std::int32_t epochYear = 0);

	// The date of the second a moment falls in. Moments before simulation time 0 are dates before
	// the epoch, not negative fields.
	Date FromSimSeconds(double seconds) const;
	// The simulation time a date starts at. Fields out of their range carry into the next one
	// (hour 25 is 1 o'clock the next day), so FromSimSeconds(ToSimSeconds(date)) normalises.
	double ToSimSeconds(const Date &date) const;
	// ISO 8601's ordinal date and time, with the day counted from 1 as it specifies:
	// "3301-043 14:05:09".
	std::string Format(const Date &date) const;

	std::int64_t SecondsPerDay() const
	{
		return m_secondsPerDay;
	}
	std::int32_t DaysPerYear() const
	{
		return m_daysPerYear;
	}
	std::int32_t EpochYear() const
	{
		return m_epochYear;
	}

  private:
	std::int64_t m_secondsPerDay;
	std::int32_t m_daysPerYear;
	std::int32_t m_epochYear;
};
} // namespace Jettison::Time
