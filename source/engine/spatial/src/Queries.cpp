#include <jettison/spatial/Queries.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include <jettison/core/Assert.h>

namespace Jettison::Spatial::Detail
{
void SortByIdentifier(std::vector<entt::entity> &entities)
{
	static_assert(sizeof(entt::id_type) == 4);
	constexpr std::size_t kRadixFrom = 512;
	if (entities.size() < kRadixFrom)
	{
		std::ranges::sort(entities, IdentifierLess);
		return;
	}
	// Least significant byte first, each pass stable. A byte every identifier shares (the
	// version's, mostly) is skipped.
	std::vector<entt::entity> scratch(entities.size());
	for (int shift = 0; shift < 32; shift += 8)
	{
		std::array<std::size_t, 256> offsets{};
		for (const entt::entity entity : entities)
			++offsets[(entt::to_integral(entity) >> shift) & 0xFFU];
		if (std::ranges::find(offsets, entities.size()) != offsets.end())
			continue;
		std::size_t total = 0;
		for (std::size_t &offset : offsets)
			total += std::exchange(offset, total);
		for (const entt::entity entity : entities)
			scratch[offsets[(entt::to_integral(entity) >> shift) & 0xFFU]++] = entity;
		entities.swap(scratch);
	}
}

NearestSet::NearestSet(std::size_t count, double maxDistance)
    : m_count(count), m_limitSquared(maxDistance * maxDistance)
{
	JETTISON_ASSERT(maxDistance >= 0.0);
	m_heap.reserve(count);
}

void NearestSet::Offer(entt::entity entity, double distanceSquared)
{
	if (m_count == 0 || distanceSquared > m_limitSquared)
		return;
	const Neighbour candidate{entity, distanceSquared};
	if (!Full())
	{
		m_heap.push_back(candidate);
		std::ranges::push_heap(m_heap, NeighbourLess);
		return;
	}
	if (!NeighbourLess(candidate, m_heap.front()))
		return;
	std::ranges::pop_heap(m_heap, NeighbourLess);
	m_heap.back() = candidate;
	std::ranges::push_heap(m_heap, NeighbourLess);
}

double NearestSet::Bound() const
{
	return Full() && m_count > 0 ? m_heap.front().distanceSquared : m_limitSquared;
}

void NearestSet::Take(std::vector<Neighbour> &out)
{
	std::ranges::sort_heap(m_heap, NeighbourLess);
	out = std::move(m_heap);
	m_heap.clear();
}
} // namespace Jettison::Spatial::Detail
