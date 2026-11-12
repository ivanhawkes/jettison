#include <jettison/spatial/SystemIndices.h>

#include <jettison/profile/Profile.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <memory>
#include <string>

namespace Jettison::Spatial
{
SystemIndices &SystemIndices::Attach(Sim::SimWorld &world, const Octree::Settings &settings)
{
	// Shared by the system, which the world keeps for as long as it lives.
	std::shared_ptr<SystemIndices> indices{new SystemIndices{settings}};
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Sim::Transform, Sim::InSystem>(),
	     {},
	     [indices](entt::registry &registry, double /*fixedStep*/) { indices->Update(registry); }});
	return *indices;
}

const SpatialIndex *SystemIndices::Find(std::uint64_t system) const
{
	const auto found = m_indices.find(system);
	return found == m_indices.end() ? nullptr : &found->second;
}

std::vector<std::uint64_t> SystemIndices::Systems() const
{
	std::vector<std::uint64_t> systems;
	systems.reserve(m_indices.size());
	for (const auto &[system, index] : m_indices)
		systems.push_back(system);
	return systems;
}

void SystemIndices::Update(const entt::registry &registry)
{
	JETTISON_PROFILE_ZONE("Spatial::SystemIndices::Update");
	++m_pass;
	const auto remove = [this](Entry &entry)
	{
		const auto index = m_indices.find(entry.system);
		index->second.Remove(entry.entity);
		if (index->second.Count() == 0)
			m_indices.erase(index);
		entry = {};
	};

	for (const auto [entity, transform, inSystem] :
	     registry.view<const Sim::Transform, const Sim::InSystem>().each())
	{
		if (inSystem.system == Sim::kNoSystem)
			continue;
		const auto slot = static_cast<std::size_t>(entt::to_entity(entity));
		if (slot >= m_entries.size())
			m_entries.resize(slot + 1);
		Entry &entry = m_entries[slot];
		if (entry.entity == entity && entry.system == inSystem.system)
			m_indices.at(entry.system).Update(entity, transform.position);
		else
		{
			// Indexed in another system, or an earlier entity at the same index.
			if (entry.entity != entt::null)
				remove(entry);
			auto index = m_indices.find(inSystem.system);
			if (index == m_indices.end())
				index = m_indices.emplace(inSystem.system, SpatialIndex{m_settings}).first;
			index->second.Insert(entity, transform.position);
			entry.entity = entity;
			entry.system = inSystem.system;
		}
		entry.pass = m_pass;
	}

	for (Entry &entry : m_entries)
		if (entry.entity != entt::null && entry.pass != m_pass)
			remove(entry);
}
} // namespace Jettison::Spatial
