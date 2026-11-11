#include <jettison/spatial/SpatialIndex.h>

#include <doctest/doctest.h>
#include <entt/entt.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <type_traits>
#include <vector>

#include <jettison/math/Random.h>

#include "Traffic.h"

using namespace Jettison;
using Spatial::LooseOctree;
using Spatial::Neighbour;
using Spatial::SortedGrid;

namespace
{
// What every structure is held to: a walk over every point, deciding with the same functions.
class BruteForce
{
  public:
	void Insert(entt::entity entity, const glm::dvec3 &position)
	{
		m_points[entt::to_integral(entity)] = {entity, position};
	}
	void Update(entt::entity entity, const glm::dvec3 &position)
	{
		m_points.at(entt::to_integral(entity)).second = position;
	}
	void Remove(entt::entity entity)
	{
		m_points.erase(entt::to_integral(entity));
	}
	std::size_t Count() const
	{
		return m_points.size();
	}

	std::vector<entt::entity> QuerySphere(const glm::dvec3 &centre, double radius) const
	{
		std::vector<entt::entity> out;
		for (const auto &[key, point] : m_points)
			if (Spatial::InSphere(point.second, centre, radius))
				out.push_back(point.first);
		return out;
	}
	std::vector<entt::entity> QueryBox(const Math::DAabb &box) const
	{
		std::vector<entt::entity> out;
		for (const auto &[key, point] : m_points)
			if (Spatial::InBox(point.second, box))
				out.push_back(point.first);
		return out;
	}
	std::vector<Neighbour> QueryNearest(
	    const glm::dvec3 &point, std::size_t count, double maxDistance) const
	{
		std::vector<Neighbour> all;
		for (const auto &[key, entry] : m_points)
		{
			const double distanceSquared = Spatial::DistanceSquared(entry.second, point);
			if (distanceSquared <= maxDistance * maxDistance)
				all.push_back({entry.first, distanceSquared});
		}
		std::ranges::sort(all, Spatial::NeighbourLess);
		all.resize(std::min(all.size(), count));
		return all;
	}

	const glm::dvec3 &Position(entt::entity entity) const
	{
		return m_points.at(entt::to_integral(entity)).second;
	}

	// The entities in identifier order.
	std::vector<entt::entity> Entities() const
	{
		std::vector<entt::entity> out;
		out.reserve(m_points.size());
		for (const auto &[key, point] : m_points)
			out.push_back(point.first);
		return out;
	}

  private:
	std::map<entt::id_type, std::pair<entt::entity, glm::dvec3>> m_points;
};

// Small settings, so that a few thousand points make a deep tree and a busy pending list.
template <typename Index> Index MakeSmall();
template <> LooseOctree MakeSmall<LooseOctree>()
{
	return LooseOctree({.halfExtent = 1.0e12, .leafCapacity = 4, .maxDepth = 36, .looseness = 2.0});
}
template <> SortedGrid MakeSmall<SortedGrid>()
{
	return SortedGrid({.cellSize = 5'000.0, .pendingLimit = 8});
}

entt::entity Entity(entt::id_type index)
{
	return entt::entity{index};
}
} // namespace

TEST_CASE_TEMPLATE("Spatial: an empty index finds nothing", Index, LooseOctree, SortedGrid)
{
	const Index index;
	CHECK(index.Count() == 0);
	CHECK_FALSE(index.Contains(Entity(0)));
	CHECK(index.QuerySphere({0.0, 0.0, 0.0}, 1.0e20).empty());
	CHECK(index.QueryBox({glm::dvec3{-1.0e20}, glm::dvec3{1.0e20}}).empty());
	CHECK(index.QueryNearest({0.0, 0.0, 0.0}, 4).empty());
}

TEST_CASE_TEMPLATE(
    "Spatial: a query keeps what is on its boundary, and orders what it finds", Index, LooseOctree,
    SortedGrid)
{
	Index index;
	// Inserted out of identifier order, in different cells and at the same place.
	index.Insert(Entity(7), {100.0, 0.0, 0.0});
	index.Insert(Entity(3), {-100.0, 0.0, 0.0});
	index.Insert(Entity(5), {0.0, 100.0, 0.0});
	index.Insert(Entity(1), {0.0, 0.0, 250.0});
	index.Insert(Entity(9), {100.0, 0.0, 0.0});
	CHECK(index.Count() == 5);

	CHECK(
	    index.QuerySphere({0.0, 0.0, 0.0}, 100.0) ==
	    std::vector<entt::entity>{Entity(3), Entity(5), Entity(7), Entity(9)});
	CHECK(index.QuerySphere({0.0, 0.0, 0.0}, 99.999).empty());
	CHECK(
	    index.QueryBox({{0.0, 0.0, 0.0}, {100.0, 100.0, 250.0}}) ==
	    std::vector<entt::entity>{Entity(1), Entity(5), Entity(7), Entity(9)});

	// Equally far, nearest goes by identifier; the limit keeps what is at it.
	CHECK(
	    index.QueryNearest({0.0, 0.0, 0.0}, 3) ==
	    std::vector<Neighbour>{{Entity(3), 1.0e4}, {Entity(5), 1.0e4}, {Entity(7), 1.0e4}});
	CHECK(index.QueryNearest({0.0, 0.0, 0.0}, 10, 100.0).size() == 4);
	CHECK(index.QueryNearest({0.0, 0.0, 0.0}, 10).size() == 5);
	CHECK(index.QueryNearest({0.0, 0.0, 0.0}, 0).empty());
	CHECK(
	    index.QueryNearest({0.0, 0.0, 1.0e9}, 1) ==
	    std::vector<Neighbour>{{Entity(1), (1.0e9 - 250.0) * (1.0e9 - 250.0)}});

	index.Update(Entity(3), {0.0, 0.0, 1.0e9});
	index.Remove(Entity(7));
	CHECK_FALSE(index.Contains(Entity(7)));
	CHECK(index.Contains(Entity(3)));
	CHECK(index.QueryNearest({0.0, 0.0, 1.0e9}, 1) == std::vector<Neighbour>{{Entity(3), 0.0}});
	CHECK(index.QuerySphere({0.0, 0.0, 0.0}, 100.0) == std::vector<entt::entity>{Entity(5), Entity(9)});
}

TEST_CASE_TEMPLATE(
    "Spatial: an index entry is the entity, version and all", Index, LooseOctree, SortedGrid)
{
	entt::registry registry;
	const entt::entity first = registry.create();
	registry.destroy(first);
	const entt::entity reused = registry.create();
	REQUIRE(entt::to_entity(first) == entt::to_entity(reused));
	REQUIRE(first != reused);

	Index index;
	index.Insert(reused, {1.0, 2.0, 3.0});
	CHECK(index.Contains(reused));
	CHECK_FALSE(index.Contains(first));
	CHECK(index.QuerySphere({1.0, 2.0, 3.0}, 0.0) == std::vector<entt::entity>{reused});
}

// The exit criterion's test: every kind of query, after every kind of change, against brute force,
// over the traffic the measurement uses and the cases a system's traffic does not reach: many points
// at one place (deeper than a leaf can split), points past the octree's root, and points that jump
// across the system in one step.
TEST_CASE_TEMPLATE(
    "Spatial: queries agree with brute force through every change", Index, LooseOctree, SortedGrid)
{
	Spatial::Test::Traffic traffic(42, 0, 4);
	Math::Random random(7, 1);
	entt::registry registry;
	Index index = MakeSmall<Index>();
	BruteForce reference;

	const auto pointFor = [&](int kind)
	{
		switch (kind)
		{
			case 0: return traffic.MakeShip().position;
			case 1: return traffic.Stations()[0] + glm::dvec3{12.5, -3.0, 7.0};
			case 2:
				return glm::dvec3{
				    traffic.Between(-5.0e13, 5.0e13), 0.0, traffic.Between(-5.0e13, 5.0e13)};
			default: return traffic.OpenSpace();
		}
	};
	const auto randomKind = [&]
	{
		const double roll = random.NextDouble();
		return roll < 0.8 ? 0 : roll < 0.9 ? 1 : roll < 0.95 ? 2 : 3;
	};

	const auto check = [&]
	{
		REQUIRE(index.Count() == reference.Count());
		for (int query = 0; query < 40; ++query)
		{
			CAPTURE(query);
			// From a ship, a station or open space, at the scales the game asks: avoidance, sensors,
			// a whole region.
			const std::vector<entt::entity> entities = reference.Entities();
			const glm::dvec3 from = random.NextDouble() < 0.7 && !entities.empty()
			                            ? reference.Position(entities[traffic.Pick(entities.size())])
			                            : pointFor(randomKind());
			const double radius = std::pow(10.0, traffic.Between(1.0, 11.0));
			CHECK(index.QuerySphere(from, radius) == reference.QuerySphere(from, radius));
			const glm::dvec3 extent{
			    radius, radius * traffic.Between(0.01, 1.0), radius * traffic.Between(0.1, 3.0)};
			const Math::DAabb box{from - extent, from + extent * traffic.Between(0.0, 2.0)};
			CHECK(index.QueryBox(box) == reference.QueryBox(box));
			const auto count = static_cast<std::size_t>(random.Range(1, 24));
			const double limit = random.NextDouble() < 0.5 ? Spatial::kUnbounded : radius;
			CHECK(index.QueryNearest(from, count, limit) == reference.QueryNearest(from, count, limit));
		}
	};

	for (int round = 0; round < 16; ++round)
	{
		CAPTURE(round);
		for (int i = 0; i < 200; ++i)
		{
			const entt::entity entity = registry.create();
			const glm::dvec3 position = pointFor(randomKind());
			index.Insert(entity, position);
			reference.Insert(entity, position);
		}
		check();

		// Every point moves: most by a step's worth, some across the system.
		for (const entt::entity entity : reference.Entities())
		{
			const glm::dvec3 position =
			    random.NextDouble() < 0.05
			        ? pointFor(randomKind())
			        : reference.Position(entity) + traffic.Direction() * traffic.Between(0.0, 3.0e4);
			index.Update(entity, position);
			reference.Update(entity, position);
		}
		check();

		// A third leave, and their identifiers are reused by the next round's.
		for (const entt::entity entity : reference.Entities())
			if (random.NextDouble() < 0.33)
			{
				index.Remove(entity);
				reference.Remove(entity);
				registry.destroy(entity);
			}
		check();
	}

	// Emptied, the index is as it was made.
	for (const entt::entity entity : reference.Entities())
	{
		index.Remove(entity);
		reference.Remove(entity);
	}
	check();
	if constexpr (std::is_same_v<Index, LooseOctree>)
		CHECK(index.NodeCount() == 1);
}

TEST_CASE("Spatial: a loose octree leaves a point in its leaf while it stays in the leaf's loose cube")
{
	LooseOctree index({.halfExtent = 1024.0, .leafCapacity = 2, .maxDepth = 8, .looseness = 2.0});
	index.Insert(Entity(1), {10.0, 10.0, 10.0});
	index.Insert(Entity(2), {-10.0, -10.0, -10.0});
	index.Insert(Entity(3), {-20.0, -20.0, -20.0});
	// The root split into eight cubes 1024 m on a side, each with a loose cube reaching 512 m past
	// its own.
	CHECK(index.NodeCount() == 9);
	index.Update(Entity(1), {-500.0, -500.0, -500.0});
	CHECK(index.NodeCount() == 9);
	CHECK(index.QueryNearest({-500.0, -500.0, -500.0}, 1) == std::vector<Neighbour>{{Entity(1), 0.0}});
	// Past its loose cube, it is placed again, with the other two, and that octant splits.
	index.Update(Entity(1), {-600.0, -600.0, -600.0});
	CHECK(index.NodeCount() == 17);
	CHECK(
	    index.QueryBox({glm::dvec3{-700.0}, glm::dvec3{-550.0}}) ==
	    std::vector<entt::entity>{Entity(1)});
	// Down to half a leaf, the tree gathers back into the root.
	index.Remove(Entity(2));
	CHECK(index.NodeCount() == 17);
	index.Remove(Entity(3));
	CHECK(index.NodeCount() == 1);
	CHECK(index.QuerySphere({0.0, 0.0, 0.0}, 2000.0) == std::vector<entt::entity>{Entity(1)});
}

TEST_CASE("Spatial: results are sorted by identifier, past the radix sort's threshold too")
{
	Math::Random random(3, 2);
	for (const std::size_t size : {0U, 1U, 511U, 512U, 5'000U})
	{
		std::vector<entt::entity> entities;
		entities.reserve(size);
		for (std::size_t i = 0; i < size; ++i)
			entities.push_back(entt::entity{static_cast<entt::id_type>(random.NextU32())});
		std::vector<entt::entity> expected = entities;
		std::ranges::sort(expected, Spatial::IdentifierLess);
		Spatial::Detail::SortByIdentifier(entities);
		CHECK(entities == expected);
	}
}
