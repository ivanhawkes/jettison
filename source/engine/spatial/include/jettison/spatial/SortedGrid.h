#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <jettison/math/Bounds.h>
#include <jettison/spatial/Queries.h>

namespace Jettison::Spatial
{
// A sorted grid over points: space cut into cubic cells of one size, which exist only where
// something is, and the points kept in one array sorted by cell, x then y then z. A box query finds
// its first cell by binary search and walks the array, and where the walk leaves the box's rows it
// searches again for the next cell inside, so its cost is in the rows that hold something rather
// than in the box's volume. A nearest query is a sphere query grown fourfold until it holds enough.
//
// A point moving within its cell is written in place. One that moves to another cell, or is new, is
// held in a short unsorted list that queries also walk, and its old place is left empty; the list
// is merged into the array once it is long enough, in one pass over the array.
class SortedGrid
{
  public:
	struct Settings
	{
		// Metres on a cell's side.
		double cellSize{10'000.0};
		// The unsorted list is merged once it holds this many, or a sixteenth of the points, if more.
		std::uint32_t pendingLimit{64};
	};

	SortedGrid();
	explicit SortedGrid(const Settings &settings);

	// As LooseOctree's.
	void Insert(entt::entity entity, const glm::dvec3 &position);
	void Update(entt::entity entity, const glm::dvec3 &position);
	void Remove(entt::entity entity);
	void Clear();

	bool Contains(entt::entity entity) const;
	std::size_t Count() const
	{
		return m_count;
	}

	void QuerySphere(const glm::dvec3 &centre, double radius, std::vector<entt::entity> &out) const;
	void QueryBox(const Math::DAabb &box, std::vector<entt::entity> &out) const;
	void QueryNearest(
	    const glm::dvec3 &point, std::size_t count, double maxDistance,
	    std::vector<Neighbour> &out) const;

	std::vector<entt::entity> QuerySphere(const glm::dvec3 &centre, double radius) const
	{
		std::vector<entt::entity> out;
		QuerySphere(centre, radius, out);
		return out;
	}
	std::vector<entt::entity> QueryBox(const Math::DAabb &box) const
	{
		std::vector<entt::entity> out;
		QueryBox(box, out);
		return out;
	}
	std::vector<Neighbour> QueryNearest(
	    const glm::dvec3 &point, std::size_t count, double maxDistance = kUnbounded) const
	{
		std::vector<Neighbour> out;
		QueryNearest(point, count, maxDistance, out);
		return out;
	}

  private:
	struct Cell
	{
		std::int64_t x{0};
		std::int64_t y{0};
		std::int64_t z{0};

		friend auto operator<=>(const Cell &, const Cell &) = default;
	};

	struct Record
	{
		Cell cell{};
		// Null for a place left empty, until the next merge.
		entt::entity entity{entt::null};
		glm::dvec3 position{0.0};
	};

	// Where an entity's record is: the sorted array, or the unsorted list.
	static constexpr std::uint32_t kPendingBit = 0x8000'0000U;

	Cell CellOf(const glm::dvec3 &position) const;
	void AddPending(const Record &record);
	void Vacate(std::uint32_t slot);
	void MergeIfDue();
	void Merge();
	// Calls visit(record) for every record in the cells the box covers, sorted and pending.
	template <typename Visit> void VisitCells(const Math::DAabb &box, Visit &&visit) const;

	Settings m_settings;
	double m_inverseCellSize;
	std::vector<Record> m_sorted;
	std::vector<Record> m_pending;
	// The merge's output, kept for its storage.
	std::vector<Record> m_merged;
	std::size_t m_vacant{0};
	std::size_t m_count{0};
	// Holds every point, though after removals not tightly, until the next merge: a nearest query
	// that has grown to cover it has seen everything.
	Math::DAabb m_extent{};
	Detail::EntitySlots m_slots;
};
} // namespace Jettison::Spatial
