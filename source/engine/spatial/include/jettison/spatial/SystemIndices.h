#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include <jettison/spatial/SpatialIndex.h>

namespace Jettison::Sim
{
class SimWorld;
}

namespace Jettison::Spatial
{
// The spatial index kept from the simulation: one SpatialIndex for each star system, of every
// entity with a Sim::Transform and a Sim::InSystem naming that system, at its Transform's position.
// An entity on a jump lane, in no system, is in none. Kept by the system "spatial" (kSystemName),
// which a system reading it runs after: it then sees every entity where the step found it, before
// anything in the step has moved it. Steering (ai/Steering.h) is its first reader.
//
// It is not state. It is made from the components every step, so a save does not hold it, and a
// world loaded from one has it from its next step; and what a query returns is decided by the
// positions alone (Queries.h), never by the order things were indexed in.
//
// The world holds it from Attach on, through the system it registers.
class SystemIndices
{
  public:
	static constexpr std::string_view kSystemName = "spatial";

	// Registers the system "spatial", with no order of its own. Throws std::logic_error as
	// SimWorld's registrations do, for one already made.
	static SystemIndices &Attach(Sim::SimWorld &world, const Octree::Settings &settings = {});

	// A system's index, or nothing for a system no entity is in.
	const SpatialIndex *Find(std::uint64_t system) const;
	// The systems with entities, in identifier order.
	std::vector<std::uint64_t> Systems() const;

	// Brings every index up to the registry: what the system does each step. An entity indexed
	// that has gone, lost its Transform or its InSystem, or is in no system is removed; one that
	// changed system moves to the other's index.
	void Update(const entt::registry &registry);

  private:
	explicit SystemIndices(const Octree::Settings &settings) : m_settings{settings}
	{
	}

	// What is indexed for an entity, by its index: EnTT keeps indices dense. The whole identifier
	// tells a stale one from an index reused at a later version.
	struct Entry
	{
		entt::entity entity{entt::null};
		std::uint64_t system{0};
		// The update that last found it.
		std::uint64_t pass{0};
	};

	Octree::Settings m_settings;
	std::map<std::uint64_t, SpatialIndex> m_indices;
	std::vector<Entry> m_entries;
	std::uint64_t m_pass{0};
};
} // namespace Jettison::Spatial
