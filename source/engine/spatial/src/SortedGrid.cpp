#include <jettison/spatial/SortedGrid.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include <jettison/core/Assert.h>

namespace Jettison::Spatial
{
namespace
{
// Cells far past any system are clamped, so a stray position cannot overflow the conversion, nor
// the row arithmetic one past it.
constexpr double kCellLimit = 4.0e18;

bool RecordLess(const auto &a, const auto &b)
{
	if (a.cell != b.cell)
		return a.cell < b.cell;
	return IdentifierLess(a.entity, b.entity);
}

void Grow(Math::DAabb &box, const glm::dvec3 &position)
{
	box.min = glm::min(box.min, position);
	box.max = glm::max(box.max, position);
}

Math::DAabb Empty()
{
	constexpr double infinity = std::numeric_limits<double>::infinity();
	return {glm::dvec3{infinity}, glm::dvec3{-infinity}};
}

bool Covers(const Math::DAabb &outer, const Math::DAabb &inner)
{
	return outer.min.x <= inner.min.x && outer.min.y <= inner.min.y && outer.min.z <= inner.min.z &&
	       outer.max.x >= inner.max.x && outer.max.y >= inner.max.y && outer.max.z >= inner.max.z;
}

// The box a sphere query walks the cells of, a little larger than the sphere's: a point InSphere
// keeps can lie, by the rounding of its distance, a few units in the last place past centre ± radius.
Math::DAabb AroundSphere(const glm::dvec3 &centre, double radius)
{
	const glm::dvec3 magnitude = glm::abs(centre);
	const double reach =
	    radius + (radius + std::max({magnitude.x, magnitude.y, magnitude.z})) * 1.0e-12;
	return {centre - reach, centre + reach};
}
} // namespace

SortedGrid::SortedGrid() : SortedGrid(Settings{})
{
}

SortedGrid::SortedGrid(const Settings &settings)
    : m_settings(settings), m_inverseCellSize(1.0 / settings.cellSize)
{
	JETTISON_VERIFY(settings.cellSize > 0.0 && std::isfinite(settings.cellSize));
	Clear();
}

void SortedGrid::Clear()
{
	m_sorted.clear();
	m_pending.clear();
	m_vacant = 0;
	m_count = 0;
	m_extent = Empty();
	m_slots.ClearAll();
}

SortedGrid::Cell SortedGrid::CellOf(const glm::dvec3 &position) const
{
	const auto axis = [this](double value)
	{
		return static_cast<std::int64_t>(
		    std::clamp(std::floor(value * m_inverseCellSize), -kCellLimit, kCellLimit));
	};
	return {axis(position.x), axis(position.y), axis(position.z)};
}

bool SortedGrid::Contains(entt::entity entity) const
{
	const std::uint32_t slot = m_slots.Find(entity);
	if (slot == Detail::EntitySlots::kNone)
		return false;
	const Record &record = (slot & kPendingBit) ? m_pending[slot & ~kPendingBit] : m_sorted[slot];
	return record.entity == entity;
}

void SortedGrid::AddPending(const Record &record)
{
	m_slots.Set(record.entity, static_cast<std::uint32_t>(m_pending.size()) | kPendingBit);
	m_pending.push_back(record);
}

void SortedGrid::Insert(entt::entity entity, const glm::dvec3 &position)
{
	JETTISON_ASSERT(!Contains(entity));
	JETTISON_ASSERT(m_slots.Find(entity) == Detail::EntitySlots::kNone);
	JETTISON_ASSERT(m_count < kPendingBit);
	AddPending({CellOf(position), entity, position});
	++m_count;
	Grow(m_extent, position);
	MergeIfDue();
}

void SortedGrid::Update(entt::entity entity, const glm::dvec3 &position)
{
	JETTISON_ASSERT(Contains(entity));
	const std::uint32_t slot = m_slots.Find(entity);
	const Cell cell = CellOf(position);
	Grow(m_extent, position);
	if (slot & kPendingBit)
	{
		Record &record = m_pending[slot & ~kPendingBit];
		record.cell = cell;
		record.position = position;
		return;
	}
	Record &record = m_sorted[slot];
	if (record.cell == cell)
	{
		record.position = position;
		return;
	}
	Vacate(slot);
	AddPending({cell, entity, position});
	MergeIfDue();
}

void SortedGrid::Remove(entt::entity entity)
{
	JETTISON_ASSERT(Contains(entity));
	const std::uint32_t slot = m_slots.Find(entity);
	if (slot & kPendingBit)
	{
		const std::uint32_t index = slot & ~kPendingBit;
		if (index != m_pending.size() - 1)
		{
			m_pending[index] = m_pending.back();
			m_slots.Set(m_pending[index].entity, index | kPendingBit);
		}
		m_pending.pop_back();
	}
	else
		Vacate(slot);
	m_slots.Clear(entity);
	--m_count;
	MergeIfDue();
}

void SortedGrid::Vacate(std::uint32_t slot)
{
	m_sorted[slot].entity = entt::null;
	++m_vacant;
}

void SortedGrid::MergeIfDue()
{
	const std::size_t limit = std::max<std::size_t>(m_settings.pendingLimit, m_count / 16);
	if (m_pending.size() + m_vacant > limit)
		Merge();
}

void SortedGrid::Merge()
{
	std::ranges::sort(m_pending, RecordLess<Record, Record>);
	m_merged.clear();
	m_merged.reserve(m_count);
	m_extent = Empty();
	auto pending = m_pending.begin();
	const auto take = [this](const Record &record)
	{
		m_slots.Set(record.entity, static_cast<std::uint32_t>(m_merged.size()));
		m_merged.push_back(record);
		Grow(m_extent, record.position);
	};
	for (const Record &record : m_sorted)
	{
		if (record.entity == entt::null)
			continue;
		while (pending != m_pending.end() && RecordLess(*pending, record))
			take(*pending++);
		take(record);
	}
	while (pending != m_pending.end())
		take(*pending++);
	std::swap(m_sorted, m_merged);
	m_pending.clear();
	m_vacant = 0;
}

template <typename Visit> void SortedGrid::VisitCells(const Math::DAabb &box, Visit &&visit) const
{
	const Cell low = CellOf(box.min);
	const Cell high = CellOf(box.max);
	const auto before = [](const Record &record, const Cell &cell) { return record.cell < cell; };
	auto at = std::lower_bound(m_sorted.begin(), m_sorted.end(), low, before);
	while (at != m_sorted.end())
	{
		const Cell &cell = at->cell;
		if (cell.x > high.x)
			break;
		// Inside the box's rows, the walk goes on; outside them, it searches for the first cell
		// that could be inside, which is past every empty row in one step.
		Cell next{};
		if (cell.y < low.y)
			next = {cell.x, low.y, low.z};
		else if (cell.y > high.y)
			next = {cell.x + 1, low.y, low.z};
		else if (cell.z < low.z)
			next = {cell.x, cell.y, low.z};
		else if (cell.z > high.z)
			next = {cell.x, cell.y + 1, low.z};
		else
		{
			if (at->entity != entt::null)
				visit(*at);
			++at;
			continue;
		}
		at = std::lower_bound(at, m_sorted.end(), next, before);
	}
	for (const Record &record : m_pending)
		visit(record);
}

void SortedGrid::QueryBox(const Math::DAabb &box, std::vector<entt::entity> &out) const
{
	out.clear();
	VisitCells(
	    box,
	    [&](const Record &record)
	    {
		    if (InBox(record.position, box))
			    out.push_back(record.entity);
	    });
	Detail::SortByIdentifier(out);
}

void SortedGrid::QuerySphere(
    const glm::dvec3 &centre, double radius, std::vector<entt::entity> &out) const
{
	out.clear();
	VisitCells(
	    AroundSphere(centre, radius),
	    [&](const Record &record)
	    {
		    if (InSphere(record.position, centre, radius))
			    out.push_back(record.entity);
	    });
	Detail::SortByIdentifier(out);
}

void SortedGrid::QueryNearest(
    const glm::dvec3 &point, std::size_t count, double maxDistance, std::vector<Neighbour> &out) const
{
	out.clear();
	if (count == 0 || m_count == 0)
		return;
	// A sphere a cell across, grown fourfold until it holds enough. Everything a sphere's cells
	// hold within its radius is seen, so once it holds count, nothing outside can be nearer; and
	// once its cells cover every point, it looks as far as it was asked to.
	for (double radius = m_settings.cellSize;; radius *= 4.0)
	{
		const double reach = std::min(radius, maxDistance);
		Math::DAabb box = AroundSphere(point, reach);
		const bool everything = Covers(box, m_extent);
		const double limit = everything ? maxDistance : reach;
		if (everything)
			box = m_extent;
		Detail::NearestSet nearest(count, limit);
		VisitCells(
		    box, [&](const Record &record)
		    { nearest.Offer(record.entity, DistanceSquared(record.position, point)); });
		if (nearest.Full() || everything || reach >= maxDistance)
		{
			nearest.Take(out);
			return;
		}
	}
}
} // namespace Jettison::Spatial
