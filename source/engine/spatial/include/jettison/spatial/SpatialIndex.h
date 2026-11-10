#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vector>

namespace Jettison::Spatial
{
// The per-system index over simulation entities for queries the simulation and AI make: nearest
// station, ships within sensor range, everything in a volume. Separate from Jolt's broadphase,
// which serves physics. Positions are system-frame doubles. Results are sorted by entity so the
// order never depends on the structure's internals (docs/coding-standards.md, "Simulation code").
// The structure, a loose octree or a sorted grid, is chosen by measurement in phase 6.
class SpatialIndex
{
  public:
	void Insert(entt::entity entity, const glm::dvec3 &position);
	void Update(entt::entity entity, const glm::dvec3 &position);
	void Remove(entt::entity entity);

	std::vector<entt::entity> QuerySphere(const glm::dvec3 &centre, double radius) const;
	std::vector<entt::entity> QueryBox(const glm::dvec3 &min, const glm::dvec3 &max) const;
	std::size_t Count() const
	{
		return m_count;
	}

  private:
	std::size_t m_count{0};
};
} // namespace Jettison::Spatial
