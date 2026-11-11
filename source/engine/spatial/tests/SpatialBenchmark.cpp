#include <jettison/spatial/SpatialIndex.h>

#include <doctest/doctest.h>
#include <entt/entt.hpp>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "Traffic.h"

using namespace Jettison;

// The measurement phase 6c chose the structure by (docs/history.md). Skipped in the gate; run it
// from a Release tree with
//
//   build/clang-release/source/engine/spatial/jettison_spatial_tests --no-skip -tc='Spatial benchmark*'
//
// Each case is a system's traffic (Traffic.h) of the given size, stepped 60 times: every ship moved
// in the index each step, then the queries a step of AI might make, from ships chosen at random.
// Avoidance looks 5 km about a ship, sensors 500 km; nearest finds a ship's eight neighbours, and
// the nearest anything from a random point in open space. The first table steps at 60 Hz; the
// second steps a second at a time, sixty times as far, which is what looseness is for.
namespace
{
using Clock = std::chrono::steady_clock;

constexpr int kSteps = 60;
constexpr int kAvoidance = 256;
constexpr int kSensors = 64;
constexpr int kNeighbours = 64;
constexpr int kOpenSpace = 16;

double Microseconds(Clock::duration duration)
{
	return std::chrono::duration<double, std::micro>(duration).count();
}

struct Timings
{
	double buildMs{0.0};
	double updateUs{0.0};
	double avoidanceUs{0.0};
	double sensorsUs{0.0};
	double neighboursUs{0.0};
	double openSpaceUs{0.0};
	std::size_t found{0};
};

void Print(const std::string &name, std::size_t count, const Timings &timings)
{
	std::printf(
	    "%-34s %7zu %9.2f %10.1f %9.2f %9.2f %9.2f %9.2f %10zu\n", name.c_str(), count, timings.buildMs,
	    timings.updateUs / kSteps, timings.avoidanceUs / (kSteps * kAvoidance),
	    timings.sensorsUs / (kSteps * kSensors), timings.neighboursUs / (kSteps * kNeighbours),
	    timings.openSpaceUs / (kSteps * kOpenSpace), timings.found);
}

// Every structure, and brute force, sees the same traffic and the same queries.
template <typename Index> Timings Measure(Index index, std::size_t count, double stepSeconds)
{
	Spatial::Test::Traffic traffic(2026, count);
	std::vector<Spatial::Test::Traffic::Ship> &ships = traffic.Ships();
	Timings timings;

	const auto built = Clock::now();
	for (std::size_t i = 0; i < count; ++i)
		index.Insert(entt::entity{static_cast<entt::id_type>(i)}, ships[i].position);
	timings.buildMs = Microseconds(Clock::now() - built) / 1000.0;

	std::vector<entt::entity> entities;
	std::vector<Spatial::Neighbour> neighbours;
	std::vector<glm::dvec3> from;
	for (int step = 0; step < kSteps; ++step)
	{
		traffic.Step(stepSeconds);
		const auto updated = Clock::now();
		for (std::size_t i = 0; i < count; ++i)
			index.Update(entt::entity{static_cast<entt::id_type>(i)}, ships[i].position);
		timings.updateUs += Microseconds(Clock::now() - updated);

		const auto timeQueries = [&](int queries, bool openSpace, double &total, auto &&query)
		{
			from.clear();
			for (int i = 0; i < queries; ++i)
				from.push_back(openSpace ? traffic.OpenSpace() : ships[traffic.Pick(count)].position);
			const auto started = Clock::now();
			for (const glm::dvec3 &point : from)
				query(point);
			total += Microseconds(Clock::now() - started);
		};
		timeQueries(
		    kAvoidance, false, timings.avoidanceUs,
		    [&](const glm::dvec3 &point)
		    {
			    index.QuerySphere(point, 5.0e3, entities);
			    timings.found += entities.size();
		    });
		timeQueries(
		    kSensors, false, timings.sensorsUs,
		    [&](const glm::dvec3 &point)
		    {
			    index.QuerySphere(point, 5.0e5, entities);
			    timings.found += entities.size();
		    });
		timeQueries(
		    kNeighbours, false, timings.neighboursUs,
		    [&](const glm::dvec3 &point)
		    {
			    index.QueryNearest(point, 8, Spatial::kUnbounded, neighbours);
			    timings.found += neighbours.size();
		    });
		timeQueries(
		    kOpenSpace, true, timings.openSpaceUs,
		    [&](const glm::dvec3 &point)
		    {
			    index.QueryNearest(point, 1, Spatial::kUnbounded, neighbours);
			    timings.found += neighbours.size();
		    });
	}
	return timings;
}

// The floor every structure has to beat: a walk over a dense array of positions.
class Scan
{
  public:
	void Insert(entt::entity entity, const glm::dvec3 &position)
	{
		m_entities.push_back(entity);
		m_positions.push_back(position);
	}
	void Update(entt::entity entity, const glm::dvec3 &position)
	{
		m_positions[static_cast<std::size_t>(entt::to_integral(entity))] = position;
	}
	void QuerySphere(const glm::dvec3 &centre, double radius, std::vector<entt::entity> &out) const
	{
		out.clear();
		for (std::size_t i = 0; i < m_positions.size(); ++i)
			if (Spatial::InSphere(m_positions[i], centre, radius))
				out.push_back(m_entities[i]);
	}
	void QueryNearest(
	    const glm::dvec3 &point, std::size_t count, double maxDistance,
	    std::vector<Spatial::Neighbour> &out) const
	{
		Spatial::Detail::NearestSet nearest(count, maxDistance);
		for (std::size_t i = 0; i < m_positions.size(); ++i)
			nearest.Offer(m_entities[i], Spatial::DistanceSquared(m_positions[i], point));
		nearest.Take(out);
	}

  private:
	std::vector<entt::entity> m_entities;
	std::vector<glm::dvec3> m_positions;
};
} // namespace

TEST_CASE(
    "Spatial benchmark: loose octree against sorted grid over a system's traffic" * doctest::skip())
{
	const auto header = [](const char *title)
	{
		std::printf(
		    "%s\n%-34s %7s %9s %10s %9s %9s %9s %9s %10s\n", title, "structure", "ships", "build ms",
		    "update us", "avoid us", "sensor us", "near8 us", "open us", "found");
	};
	const auto octree = [](std::uint32_t capacity, double looseness, std::size_t count, double step)
	{
		Spatial::LooseOctree::Settings settings;
		settings.leafCapacity = capacity;
		settings.looseness = looseness;
		char name[64];
		std::snprintf(name, sizeof name, "octree leaf %u loose %.1f", capacity, looseness);
		Print(name, count, Measure(Spatial::LooseOctree(settings), count, step));
	};
	const auto grid = [](double cellSize, std::size_t count, double step)
	{
		Spatial::SortedGrid::Settings settings;
		settings.cellSize = cellSize;
		char name[64];
		std::snprintf(name, sizeof name, "grid cell %.0f km", cellSize / 1000.0);
		Print(name, count, Measure(Spatial::SortedGrid(settings), count, step));
	};

	header("Steps of 1/60 s");
	for (const std::size_t count : {1'000U, 10'000U, 100'000U})
	{
		for (const std::uint32_t capacity : {16U, 32U, 64U})
			for (const double looseness : {1.0, 1.5, 2.0})
				octree(capacity, looseness, count, 1.0 / 60.0);
		for (const double cellSize : {1.0e3, 1.0e4, 1.0e5, 1.0e6})
			grid(cellSize, count, 1.0 / 60.0);
		if (count <= 10'000U)
			Print("scan", count, Measure(Scan{}, count, 1.0 / 60.0));
		std::printf("\n");
	}

	header("Steps of 1 s");
	for (const std::size_t count : {10'000U, 100'000U})
	{
		for (const double looseness : {1.0, 1.5, 2.0})
			octree(32, looseness, count, 1.0);
		grid(1.0e6, count, 1.0);
		std::printf("\n");
	}
}
