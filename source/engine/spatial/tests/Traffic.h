#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

#include <jettison/math/Random.h>

namespace Jettison::Spatial::Test
{
// A star system's traffic, as the index will see it: most ships about a few stations, some along
// the lanes between them, and a scatter across the disc. Y is up, so the disc is in XZ.
//
// Stations are 50 to 400 million km from the star. Station traffic is within about 60 km of its
// station, at 20 to 200 m/s; lane traffic within a kilometre of the line between two stations, at
// 20 to 300 km/s along it; the scatter anywhere in the disc out to 450 million km and 2 million km
// above or below it, drifting at up to 50 m/s.
class Traffic
{
  public:
	struct Ship
	{
		glm::dvec3 position{0.0};
		glm::dvec3 velocity{0.0};
	};

	Traffic(std::uint64_t seed, std::size_t count, std::size_t stationCount = 8) : m_random(seed, 6)
	{
		for (std::size_t i = 0; i < stationCount; ++i)
		{
			const double radius = Between(5.0e10, 4.0e11);
			const double angle = Between(0.0, 2.0 * std::numbers::pi);
			m_stations.emplace_back(
			    radius * std::cos(angle), Between(-1.0e9, 1.0e9), radius * std::sin(angle));
		}
		m_ships.reserve(count);
		for (std::size_t i = 0; i < count; ++i)
			m_ships.push_back(MakeShip());
	}

	const std::vector<glm::dvec3> &Stations() const
	{
		return m_stations;
	}
	std::vector<Ship> &Ships()
	{
		return m_ships;
	}

	Ship MakeShip()
	{
		const double kind = m_random.NextDouble();
		if (kind < 0.6)
		{
			const glm::dvec3 &station = m_stations[Pick(m_stations.size())];
			const glm::dvec3 offset{Bell(), Bell(), Bell()};
			return {station + offset * 20'000.0, Moving(20.0, 200.0)};
		}
		if (kind < 0.9)
		{
			const std::size_t from = Pick(m_stations.size());
			const std::size_t to = (from + 1 + Pick(m_stations.size() - 1)) % m_stations.size();
			const glm::dvec3 lane = m_stations[to] - m_stations[from];
			const double along = m_random.NextDouble();
			const glm::dvec3 position = m_stations[from] + lane * along + Direction() * 1'000.0;
			return {position, glm::normalize(lane) * Between(2.0e4, 3.0e5)};
		}
		const glm::dvec3 position = InDisc();
		return {position, Moving(0.0, 50.0)};
	}

	void Step(double seconds)
	{
		for (Ship &ship : m_ships)
			ship.position += ship.velocity * seconds;
	}

	// A point anywhere in the disc, for a query from open space.
	glm::dvec3 OpenSpace()
	{
		return InDisc();
	}

	std::size_t Pick(std::size_t count)
	{
		return static_cast<std::size_t>(m_random.Range(0, static_cast<std::int32_t>(count) - 1));
	}
	double Between(double min, double max)
	{
		return min + (max - min) * m_random.NextDouble();
	}
	glm::dvec3 Direction()
	{
		for (;;)
		{
			const glm::dvec3 d{Between(-1.0, 1.0), Between(-1.0, 1.0), Between(-1.0, 1.0)};
			const double length = glm::length(d);
			if (length > 0.01 && length <= 1.0)
				return d / length;
		}
	}

	// A random direction at a random speed. Every draw is its own statement, as everywhere here: the
	// operands of one expression are evaluated in an order the compiler chooses.
	glm::dvec3 Moving(double minSpeed, double maxSpeed)
	{
		const glm::dvec3 direction = Direction();
		return direction * Between(minSpeed, maxSpeed);
	}

  private:
	// Roughly normal, mean 0 and deviation 1, and never past 3.
	double Bell()
	{
		double sum = m_random.NextDouble();
		sum += m_random.NextDouble();
		sum += m_random.NextDouble();
		return (sum - 1.5) * 2.0;
	}
	glm::dvec3 InDisc()
	{
		const double radius = 4.5e11 * std::sqrt(m_random.NextDouble());
		const double angle = Between(0.0, 2.0 * std::numbers::pi);
		return {radius * std::cos(angle), Between(-2.0e9, 2.0e9), radius * std::sin(angle)};
	}

	Math::Random m_random;
	std::vector<glm::dvec3> m_stations;
	std::vector<Ship> m_ships;
};
} // namespace Jettison::Spatial::Test
