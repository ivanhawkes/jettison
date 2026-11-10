#pragma once

#include <cstdint>
#include <string>

namespace Jettison::Time
{
// Simulation seconds as an in-game date. The epoch and the calendar's shape (day length, months)
// are data the game supplies; the engine does the arithmetic. Filled in phase 2.
struct Date
{
	std::int32_t year{0};
	std::int32_t day{0}; // Day of the year, from 0.
	std::int32_t hour{0};
	std::int32_t minute{0};
	std::int32_t second{0};
};

class Calendar
{
  public:
	Calendar(double secondsPerDay, std::int32_t daysPerYear);

	Date FromSimSeconds(double seconds) const;
	double ToSimSeconds(const Date &date) const;
	std::string Format(const Date &date) const;

	double SecondsPerDay() const
	{
		return m_secondsPerDay;
	}
	std::int32_t DaysPerYear() const
	{
		return m_daysPerYear;
	}

  private:
	double m_secondsPerDay;
	std::int32_t m_daysPerYear;
};
} // namespace Jettison::Time
