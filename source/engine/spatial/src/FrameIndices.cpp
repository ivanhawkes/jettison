#include <jettison/spatial/FrameIndices.h>

#include <jettison/profile/Profile.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include <limits>
#include <memory>
#include <string>

namespace Jettison::Spatial
{
FrameIndices &FrameIndices::Attach(Sim::SimWorld &world, const Octree::Settings &settings)
{
	// Shared by the system, which the world keeps for as long as it lives.
	std::shared_ptr<FrameIndices> indices{new FrameIndices{settings}};
	world.RegisterSystem(
	    {std::string{kSystemName},
	     Ecs::TickPhase::Simulation,
	     Ecs::Access::Of<Sim::Transform, Sim::InFrame>(),
	     {},
	     [indices](entt::registry &registry, double /*fixedStep*/) { indices->Update(registry); }},
	    // Not state: a load starts it empty, so nothing reads it before its update in a step, which
	    // finds every entity where the steps passed over left it.
	    {.next = [](const Sim::SimWorld &) { return std::numeric_limits<std::uint64_t>::max(); }});
	return *indices;
}


const SpatialIndex *FrameIndices::Find(std::uint64_t frame) const
{
	const auto found = m_indices.find(frame);
	return found == m_indices.end() ? nullptr : &found->second;
}


std::vector<std::uint64_t> FrameIndices::Frames() const
{
	std::vector<std::uint64_t> frames;
	frames.reserve(m_indices.size());
	for (const auto &[frame, index] : m_indices)
		frames.push_back(frame);
	return frames;
}


void FrameIndices::Update(const entt::registry &registry)
{
	JETTISON_PROFILE_ZONE("Spatial::FrameIndices::Update");
	++m_pass;
	const auto remove = [this](Entry &entry)
	{
		const auto index = m_indices.find(entry.frame);
		index->second.Remove(entry.entity);
		if (index->second.Count() == 0)
			m_indices.erase(index);
		entry = {};
	};

	for (const auto [entity, transform, inFrame] :
	     registry.view<const Sim::Transform, const Sim::InFrame>().each())
	{
		if (inFrame.frame == Sim::kNoFrame)
			continue;
		const auto slot = static_cast<std::size_t>(entt::to_entity(entity));
		if (slot >= m_entries.size())
			m_entries.resize(slot + 1);
		Entry &entry = m_entries[slot];
		if (entry.entity == entity && entry.frame == inFrame.frame)
			m_indices.at(entry.frame).Update(entity, transform.position);
		else
		{
			// Indexed in another frame, or an earlier entity at the same index.
			if (entry.entity != entt::null)
				remove(entry);
			auto index = m_indices.find(inFrame.frame);
			if (index == m_indices.end())
				index = m_indices.emplace(inFrame.frame, SpatialIndex{m_settings}).first;
			index->second.Insert(entity, transform.position);
			entry.entity = entity;
			entry.frame = inFrame.frame;
		}
		entry.pass = m_pass;
	}

	for (Entry &entry : m_entries)
		if (entry.entity != entt::null && entry.pass != m_pass)
			remove(entry);
}
} // namespace Jettison::Spatial
