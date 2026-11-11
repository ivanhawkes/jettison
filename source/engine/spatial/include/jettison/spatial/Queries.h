#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <vector>

#include <jettison/math/Bounds.h>

namespace Jettison::Spatial
{
// What a spatial index asks and how it answers, shared by the structure kept and any measured
// against it.
// Entities are points in the system frame, in metres (docs/architecture.md, "Coordinate frames and
// units"). A query for things with extent, an obstacle and its radius, widens its own radius by the
// largest extent it cares about and tests the extent itself.
//
// Every structure decides membership with the functions below, and nothing else, so a structure and
// the brute-force walk its tests compare it with agree to the last bit, and its own bounds can be
// shown to prune nothing those functions would keep (each is a sum of correctly rounded, monotonic
// steps, done in the same order).

// One of the nearest entities to a point, and how far it is.
struct Neighbour
{
	entt::entity entity{entt::null};
	double distanceSquared{0.0};

	friend bool operator==(const Neighbour &, const Neighbour &) = default;
};

// No limit to how far a nearest query looks.
inline constexpr double kUnbounded = std::numeric_limits<double>::infinity();

// The squared distance between two points: (dx² + dy²) + dz², in that order.
inline double DistanceSquared(const glm::dvec3 &a, const glm::dvec3 &b)
{
	const glm::dvec3 d = a - b;
	return d.x * d.x + d.y * d.y + d.z * d.z;
}

// The squared distance from a point to the nearest point of a box, zero inside it. For a point in
// the box it is no more than DistanceSquared to that point, axis by axis, rounding included.
inline double DistanceSquared(const glm::dvec3 &point, const Math::DAabb &box)
{
	glm::dvec3 gap{0.0};
	for (int axis = 0; axis < 3; ++axis)
	{
		if (point[axis] < box.min[axis])
			gap[axis] = box.min[axis] - point[axis];
		else if (point[axis] > box.max[axis])
			gap[axis] = point[axis] - box.max[axis];
	}
	return gap.x * gap.x + gap.y * gap.y + gap.z * gap.z;
}

// A sphere query keeps what is at the radius; a box query keeps what is on its faces.
inline bool InSphere(const glm::dvec3 &point, const glm::dvec3 &centre, double radius)
{
	return DistanceSquared(point, centre) <= radius * radius;
}

inline bool InBox(const glm::dvec3 &point, const Math::DAabb &box)
{
	return point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y &&
	       point.y <= box.max.y && point.z >= box.min.z && point.z <= box.max.z;
}

inline bool Overlaps(const Math::DAabb &a, const Math::DAabb &b)
{
	return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
	       a.min.z <= b.max.z && a.max.z >= b.min.z;
}

// Range results are in identifier order, as Sim::Snapshot keeps its transforms, so what a query
// returns never depends on the structure's internals (docs/coding-standards.md, "Simulation code").
inline bool IdentifierLess(entt::entity a, entt::entity b)
{
	return entt::to_integral(a) < entt::to_integral(b);
}

// Nearest results are by distance, then by identifier: two entities equally far are always given in
// the same order, and the same one is kept when only one fits.
inline bool NeighbourLess(const Neighbour &a, const Neighbour &b)
{
	if (a.distanceSquared != b.distanceSquared)
		return a.distanceSquared < b.distanceSquared;
	return IdentifierLess(a.entity, b.entity);
}

namespace Detail
{
// Puts a range query's results in IdentifierLess order. Above a few hundred it is a radix sort,
// which a sensor sweep about a busy station needs: measured in 6c, std::sort of the 7,500 entities
// such a query finds was most of the query.
void SortByIdentifier(std::vector<entt::entity> &entities);

// Where each entity's record is, by the entity's index: EnTT keeps indices dense, so this is a
// vector, not a hash map, and a lookup is one load. The record keeps the whole identifier, which is
// how a stale one (an index reused at a later version) is told from the entity indexed.
class EntitySlots
{
  public:
	static constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();

	std::uint32_t Find(entt::entity entity) const
	{
		const auto index = static_cast<std::size_t>(entt::to_entity(entity));
		return index < m_slots.size() ? m_slots[index] : kNone;
	}
	void Set(entt::entity entity, std::uint32_t slot)
	{
		const auto index = static_cast<std::size_t>(entt::to_entity(entity));
		if (index >= m_slots.size())
			m_slots.resize(index + 1, kNone);
		m_slots[index] = slot;
	}
	void Clear(entt::entity entity)
	{
		m_slots[static_cast<std::size_t>(entt::to_entity(entity))] = kNone;
	}
	void ClearAll()
	{
		m_slots.clear();
	}

  private:
	std::vector<std::uint32_t> m_slots;
};

// Keeps the count nearest of what it is offered, by NeighbourLess, as a max-heap with the farthest
// on top; Bound is how far a candidate may be and still be kept.
class NearestSet
{
  public:
	NearestSet(std::size_t count, double maxDistance);

	void Offer(entt::entity entity, double distanceSquared);
	bool Full() const
	{
		return m_heap.size() == m_count;
	}
	// The squared distance past which nothing can be kept: the farthest kept once full, the limit
	// until then. Something exactly this far may still be kept, on its identifier.
	double Bound() const;
	// What was kept, nearest first.
	void Take(std::vector<Neighbour> &out);

  private:
	std::size_t m_count;
	double m_limitSquared;
	std::vector<Neighbour> m_heap;
};
} // namespace Detail
} // namespace Jettison::Spatial
