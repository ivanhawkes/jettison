#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <jettison/math/Bounds.h>
#include <jettison/spatial/Queries.h>

namespace Jettison::Spatial
{
// An octree over points, the structure phase 6c kept as the spatial index (SpatialIndex.h). A
// node's cube is split in eight when it holds more than a leaf's capacity, and its children are
// gathered back into it when their subtree holds half that, so a station's traffic is in small
// leaves and the space between planets in large ones.
//
// It can be a loose octree: each node then has a loose cube, its own grown by the looseness about
// its centre, a point stays in its leaf while it is inside the leaf's loose cube, and queries prune
// by the loose cubes, which hold everything their nodes do. Measured in 6c, looseness bought nothing
// for traffic stepped at 60 Hz and made every small query slower, as loose cubes overlap; it is
// kept as a setting, at 1, for traffic that moves farther in a step.
//
// The root's cube is a power of two metres on a side, centred on the system's origin, so that every
// node's centre and size, and its loose cube, are exact in double: halving them never rounds. A
// point outside the root's cube is held by the root itself, which every query visits.
class Octree
{
  public:
	struct Settings
	{
		// Half the root cube's side, rounded up to a power of two: 2^43 m, 59 AU, by default.
		double halfExtent{8.796093022208e12};
		// A leaf holding more points than this is split, unless it is at the deepest level. 64 was
		// the best measured in 6c: at 100,000 ships 32 was within a tenth of it, and 16 a third slower
		// to update.
		std::uint32_t leafCapacity{64};
		// 2^43 m halved 36 times is a leaf 256 m on a side.
		std::uint32_t maxDepth{36};
		// The loose cube's half side over the cube's: 1 is a plain octree. With steps of a second
		// (sixty times the 60 Hz step's motion), 2 saved a quarter of the update and cost 2.5 to 4
		// times as much on a 5 km avoidance query and on nearest queries.
		double looseness{1.0};
	};

	Octree();
	explicit Octree(const Settings &settings);

	// An entity is inserted once and removed once; inserting one already indexed, or updating or
	// removing one that is not, is a programming error (JETTISON_ASSERT). A point a step moves within
	// its leaf is written in place.
	void Insert(entt::entity entity, const glm::dvec3 &position);
	void Update(entt::entity entity, const glm::dvec3 &position);
	void Remove(entt::entity entity);
	void Clear();

	bool Contains(entt::entity entity) const;
	std::size_t Count() const
	{
		return m_items.size();
	}

	// Everything within the radius of the centre (at it included), or in the box (on its faces
	// included), in identifier order. The forms taking a vector clear it and reuse its storage.
	void QuerySphere(const glm::dvec3 &centre, double radius, std::vector<entt::entity> &out) const;
	void QueryBox(const Math::DAabb &box, std::vector<entt::entity> &out) const;
	// The count nearest to the point within maxDistance (at it included), nearest first, entities
	// equally far in identifier order.
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

	// The nodes in use, leaves and branches, for the tests and the measurement.
	std::size_t NodeCount() const
	{
		return m_nodes.size() - 8 * m_freeBlocks.size();
	}

  private:
	static constexpr std::uint32_t kNone = Detail::EntitySlots::kNone;

	struct Node
	{
		glm::dvec3 centre{0.0};
		double halfSize{0.0};
		// Its cube grown by the looseness: what a query tests and what a point may wander in.
		Math::DAabb loose{};
		std::uint32_t parent{kNone};
		// The first of eight children, consecutive; kNone for a leaf.
		std::uint32_t firstChild{kNone};
		std::uint32_t depth{0};
		// The points in this node and below it.
		std::uint32_t subtreeCount{0};
		// Indices into m_items. Only a leaf holds points, but for the root, which also holds those
		// outside its cube.
		std::vector<std::uint32_t> items;
	};

	struct Item
	{
		entt::entity entity{entt::null};
		glm::dvec3 position{0.0};
		std::uint32_t node{kNone};
		// Where it is in its node's items.
		std::uint32_t indexInNode{0};
	};

	bool InRootCube(const glm::dvec3 &position) const;
	void Place(std::uint32_t item);
	void Unplace(std::uint32_t item);
	void AddToNode(std::uint32_t node, std::uint32_t item);
	void RemoveFromNode(std::uint32_t item);
	void Split(std::uint32_t node);
	void Collapse(std::uint32_t node);
	void FreeChildren(std::uint32_t node);
	void MakeNode(std::uint32_t index, std::uint32_t parent, const glm::dvec3 &centre, double halfSize);

	Settings m_settings;
	// Node 0 is the root. Children are allocated eight at a time, and a freed block of eight is
	// reused before the vector grows.
	std::vector<Node> m_nodes;
	std::vector<std::uint32_t> m_freeBlocks;
	// Dense: a removed item's place is taken by the last.
	std::vector<Item> m_items;
	Detail::EntitySlots m_slots;
};
} // namespace Jettison::Spatial
