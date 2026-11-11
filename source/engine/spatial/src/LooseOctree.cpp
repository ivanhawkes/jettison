#include <jettison/spatial/LooseOctree.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <jettison/core/Assert.h>

namespace Jettison::Spatial
{
namespace
{
// Bounds the traversal stack: a depth-first walk holds at most seven siblings a level and the
// eight children of the node it is at.
constexpr std::uint32_t kDeepestLevel = 48;
constexpr std::size_t kStackSize = std::size_t{8} * (kDeepestLevel + 1);

double PowerOfTwoAtLeast(double value)
{
	int exponent = 0;
	const double mantissa = std::frexp(value, &exponent);
	return std::ldexp(1.0, mantissa == 0.5 ? exponent - 1 : exponent);
}

std::uint32_t Octant(const glm::dvec3 &centre, const glm::dvec3 &position)
{
	return (position.x >= centre.x ? 1U : 0U) | (position.y >= centre.y ? 2U : 0U) |
	       (position.z >= centre.z ? 4U : 0U);
}

glm::dvec3 OctantDirection(std::uint32_t octant)
{
	return {(octant & 1U) ? 1.0 : -1.0, (octant & 2U) ? 1.0 : -1.0, (octant & 4U) ? 1.0 : -1.0};
}

bool InCube(const glm::dvec3 &position, const glm::dvec3 &centre, double halfSize)
{
	return InBox(position, {centre - halfSize, centre + halfSize});
}

// A fixed stack, so a query allocates nothing but its results.
class NodeStack
{
  public:
	void Push(std::uint32_t node)
	{
		JETTISON_ASSERT(m_size < m_nodes.size());
		m_nodes[m_size++] = node;
	}
	std::uint32_t Pop()
	{
		return m_nodes[--m_size];
	}
	bool Empty() const
	{
		return m_size == 0;
	}

  private:
	std::array<std::uint32_t, kStackSize> m_nodes{};
	std::size_t m_size{0};
};
} // namespace

LooseOctree::LooseOctree() : LooseOctree(Settings{})
{
}

LooseOctree::LooseOctree(const Settings &settings) : m_settings(settings)
{
	JETTISON_VERIFY(settings.halfExtent > 0.0 && std::isfinite(settings.halfExtent));
	JETTISON_VERIFY(settings.leafCapacity > 0);
	JETTISON_VERIFY(settings.maxDepth <= kDeepestLevel);
	JETTISON_VERIFY(settings.looseness >= 1.0);
	m_settings.halfExtent = PowerOfTwoAtLeast(settings.halfExtent);
	Clear();
}

void LooseOctree::Clear()
{
	m_nodes.clear();
	m_freeBlocks.clear();
	m_items.clear();
	m_slots.ClearAll();
	m_nodes.emplace_back();
	MakeNode(0, kNone, glm::dvec3{0.0}, m_settings.halfExtent);
}

void LooseOctree::MakeNode(
    std::uint32_t index, std::uint32_t parent, const glm::dvec3 &centre, double halfSize)
{
	Node &node = m_nodes[index];
	node.centre = centre;
	node.halfSize = halfSize;
	const double looseHalf = halfSize * m_settings.looseness;
	node.loose = {centre - looseHalf, centre + looseHalf};
	node.parent = parent;
	node.firstChild = kNone;
	node.depth = parent == kNone ? 0 : m_nodes[parent].depth + 1;
	node.subtreeCount = 0;
	node.items.clear();
}

bool LooseOctree::Contains(entt::entity entity) const
{
	const std::uint32_t slot = m_slots.Find(entity);
	return slot != kNone && m_items[slot].entity == entity;
}

bool LooseOctree::InRootCube(const glm::dvec3 &position) const
{
	return InCube(position, m_nodes[0].centre, m_nodes[0].halfSize);
}

void LooseOctree::Insert(entt::entity entity, const glm::dvec3 &position)
{
	JETTISON_ASSERT(!Contains(entity));
	JETTISON_ASSERT(m_slots.Find(entity) == kNone);
	const auto index = static_cast<std::uint32_t>(m_items.size());
	m_items.push_back({entity, position, kNone, 0});
	m_slots.Set(entity, index);
	Place(index);
}

void LooseOctree::Update(entt::entity entity, const glm::dvec3 &position)
{
	JETTISON_ASSERT(Contains(entity));
	const std::uint32_t index = m_slots.Find(entity);
	Item &item = m_items[index];
	const Node &node = m_nodes[item.node];
	// The root holds everything while it is a leaf, and what is outside its cube once it is not.
	const bool stays = item.node == 0 ? node.firstChild == kNone || !InRootCube(position)
	                                  : InBox(position, node.loose);
	if (stays)
	{
		item.position = position;
		return;
	}
	Unplace(index);
	m_items[index].position = position;
	Place(index);
}

void LooseOctree::Remove(entt::entity entity)
{
	JETTISON_ASSERT(Contains(entity));
	const std::uint32_t index = m_slots.Find(entity);
	Unplace(index);
	const auto last = static_cast<std::uint32_t>(m_items.size() - 1);
	if (index != last)
	{
		m_items[index] = m_items[last];
		const Item &moved = m_items[index];
		m_nodes[moved.node].items[moved.indexInNode] = index;
		m_slots.Set(moved.entity, index);
	}
	m_items.pop_back();
	m_slots.Clear(entity);
}

void LooseOctree::AddToNode(std::uint32_t node, std::uint32_t item)
{
	std::vector<std::uint32_t> &items = m_nodes[node].items;
	m_items[item].node = node;
	m_items[item].indexInNode = static_cast<std::uint32_t>(items.size());
	items.push_back(item);
}

void LooseOctree::RemoveFromNode(std::uint32_t item)
{
	const Item &removed = m_items[item];
	std::vector<std::uint32_t> &items = m_nodes[removed.node].items;
	const std::uint32_t last = items.back();
	items[removed.indexInNode] = last;
	m_items[last].indexInNode = removed.indexInNode;
	items.pop_back();
	m_items[item].node = kNone;
}

void LooseOctree::Place(std::uint32_t item)
{
	const glm::dvec3 position = m_items[item].position;
	std::uint32_t node = 0;
	++m_nodes[0].subtreeCount;
	if (InRootCube(position))
	{
		while (m_nodes[node].firstChild != kNone)
		{
			node = m_nodes[node].firstChild + Octant(m_nodes[node].centre, position);
			++m_nodes[node].subtreeCount;
		}
	}
	AddToNode(node, item);
	const Node &leaf = m_nodes[node];
	if (leaf.firstChild == kNone && leaf.items.size() > m_settings.leafCapacity &&
	    leaf.depth < m_settings.maxDepth)
		Split(node);
}

void LooseOctree::Unplace(std::uint32_t item)
{
	const std::uint32_t leaf = m_items[item].node;
	RemoveFromNode(item);
	for (std::uint32_t node = leaf; node != kNone; node = m_nodes[node].parent)
		--m_nodes[node].subtreeCount;

	// The highest branch above the leaf that now holds no more than half a leaf is gathered into
	// one. The counts only grow going up, so the walk stops at the first that holds more.
	const std::uint32_t threshold = m_settings.leafCapacity / 2;
	std::uint32_t gather = kNone;
	for (std::uint32_t node = m_nodes[leaf].parent;
	     node != kNone && m_nodes[node].subtreeCount <= threshold; node = m_nodes[node].parent)
		gather = node;
	if (gather != kNone)
		Collapse(gather);
}

void LooseOctree::Split(std::uint32_t node)
{
	std::uint32_t first = 0;
	if (!m_freeBlocks.empty())
	{
		first = m_freeBlocks.back();
		m_freeBlocks.pop_back();
	}
	else
	{
		first = static_cast<std::uint32_t>(m_nodes.size());
		m_nodes.resize(m_nodes.size() + 8);
	}
	const double childHalf = m_nodes[node].halfSize * 0.5;
	for (std::uint32_t octant = 0; octant < 8; ++octant)
		MakeNode(
		    first + octant, node, m_nodes[node].centre + OctantDirection(octant) * childHalf,
		    childHalf);
	m_nodes[node].firstChild = first;

	const std::vector<std::uint32_t> items = std::exchange(m_nodes[node].items, {});
	std::vector<std::uint32_t> wanderers;
	for (const std::uint32_t item : items)
	{
		const glm::dvec3 &position = m_items[item].position;
		if (node == 0 && !InRootCube(position))
			AddToNode(0, item);
		else if (InCube(position, m_nodes[node].centre, m_nodes[node].halfSize))
		{
			const std::uint32_t child = first + Octant(m_nodes[node].centre, position);
			AddToNode(child, item);
			++m_nodes[child].subtreeCount;
		}
		else
			wanderers.push_back(item);
	}

	// A point that had wandered into this leaf's loose cube from outside its cube belongs to no
	// child, so it is placed again from the root, once the children are complete.
	for (const std::uint32_t item : wanderers)
	{
		for (std::uint32_t above = node; above != kNone; above = m_nodes[above].parent)
			--m_nodes[above].subtreeCount;
		m_items[item].node = kNone;
		Place(item);
	}

	for (std::uint32_t octant = 0; octant < 8; ++octant)
	{
		const Node &child = m_nodes[first + octant];
		if (child.firstChild == kNone && child.items.size() > m_settings.leafCapacity &&
		    child.depth < m_settings.maxDepth)
			Split(first + octant);
	}
}

void LooseOctree::Collapse(std::uint32_t node)
{
	// Every point below the node is inside its loose cube, since a child's loose cube is inside its
	// parent's for any looseness of 1 or more: h/2 + L·h/2 <= L·h.
	NodeStack stack;
	stack.Push(m_nodes[node].firstChild);
	std::vector<std::uint32_t> gathered;
	gathered.reserve(m_nodes[node].subtreeCount);
	for (std::uint32_t octant = 1; octant < 8; ++octant)
		stack.Push(m_nodes[node].firstChild + octant);
	while (!stack.Empty())
	{
		const std::uint32_t below = stack.Pop();
		const Node &child = m_nodes[below];
		gathered.insert(gathered.end(), child.items.begin(), child.items.end());
		if (child.firstChild != kNone)
			for (std::uint32_t octant = 0; octant < 8; ++octant)
				stack.Push(child.firstChild + octant);
	}
	FreeChildren(node);
	for (const std::uint32_t item : gathered)
		AddToNode(node, item);
}

void LooseOctree::FreeChildren(std::uint32_t node)
{
	const std::uint32_t first = m_nodes[node].firstChild;
	for (std::uint32_t octant = 0; octant < 8; ++octant)
	{
		Node &child = m_nodes[first + octant];
		if (child.firstChild != kNone)
			FreeChildren(first + octant);
		child.items.clear();
		child.subtreeCount = 0;
	}
	m_freeBlocks.push_back(first);
	m_nodes[node].firstChild = kNone;
}

void LooseOctree::QueryBox(const Math::DAabb &box, std::vector<entt::entity> &out) const
{
	out.clear();
	NodeStack stack;
	stack.Push(0);
	while (!stack.Empty())
	{
		const Node &node = m_nodes[stack.Pop()];
		for (const std::uint32_t index : node.items)
			if (InBox(m_items[index].position, box))
				out.push_back(m_items[index].entity);
		if (node.firstChild == kNone)
			continue;
		for (std::uint32_t octant = 0; octant < 8; ++octant)
		{
			const Node &child = m_nodes[node.firstChild + octant];
			if (child.subtreeCount > 0 && Overlaps(child.loose, box))
				stack.Push(node.firstChild + octant);
		}
	}
	Detail::SortByIdentifier(out);
}

void LooseOctree::QuerySphere(
    const glm::dvec3 &centre, double radius, std::vector<entt::entity> &out) const
{
	out.clear();
	const double radiusSquared = radius * radius;
	NodeStack stack;
	stack.Push(0);
	while (!stack.Empty())
	{
		const Node &node = m_nodes[stack.Pop()];
		for (const std::uint32_t index : node.items)
			if (InSphere(m_items[index].position, centre, radius))
				out.push_back(m_items[index].entity);
		if (node.firstChild == kNone)
			continue;
		for (std::uint32_t octant = 0; octant < 8; ++octant)
		{
			const Node &child = m_nodes[node.firstChild + octant];
			if (child.subtreeCount > 0 && DistanceSquared(centre, child.loose) <= radiusSquared)
				stack.Push(node.firstChild + octant);
		}
	}
	Detail::SortByIdentifier(out);
}

void LooseOctree::QueryNearest(
    const glm::dvec3 &point, std::size_t count, double maxDistance, std::vector<Neighbour> &out) const
{
	out.clear();
	if (count == 0)
		return;
	Detail::NearestSet nearest(count, maxDistance);

	// Best first: the node nearest the point is opened next, and the walk ends when the nearest
	// unopened node is farther than anything the set could still keep. A node exactly that far is
	// opened, for an entity there that wins on its identifier.
	struct Open
	{
		double distanceSquared;
		std::uint32_t node;
	};
	const auto farther = [](const Open &a, const Open &b)
	{ return a.distanceSquared > b.distanceSquared; };
	std::vector<Open> open;
	open.reserve(64);
	open.push_back({0.0, 0});
	while (!open.empty())
	{
		std::ranges::pop_heap(open, farther);
		const Open next = open.back();
		open.pop_back();
		if (next.distanceSquared > nearest.Bound())
			break;
		const Node &node = m_nodes[next.node];
		for (const std::uint32_t index : node.items)
			nearest.Offer(m_items[index].entity, DistanceSquared(m_items[index].position, point));
		if (node.firstChild == kNone)
			continue;
		for (std::uint32_t octant = 0; octant < 8; ++octant)
		{
			const Node &child = m_nodes[node.firstChild + octant];
			if (child.subtreeCount == 0)
				continue;
			const double distanceSquared = DistanceSquared(point, child.loose);
			if (distanceSquared <= nearest.Bound())
			{
				open.push_back({distanceSquared, node.firstChild + octant});
				std::ranges::push_heap(open, farther);
			}
		}
	}
	nearest.Take(out);
}
} // namespace Jettison::Spatial
