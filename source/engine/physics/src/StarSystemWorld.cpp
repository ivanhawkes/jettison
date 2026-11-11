#include "StarSystemWorld.h"

#include <utility>

#include <jettison/core/Assert.h>

namespace Jettison::Physics::Detail
{
Layers::Layers()
{
	pairs.EnableCollision(kMoving, kStatic);
	pairs.EnableCollision(kMoving, kMoving);
	broadPhase.MapObjectToBroadPhaseLayer(kStatic, JPH::BroadPhaseLayer{0});
	broadPhase.MapObjectToBroadPhaseLayer(kMoving, JPH::BroadPhaseLayer{1});
	objectVsBroadPhase.emplace(broadPhase, 2, pairs, 2);
}

StarSystemWorld::StarSystemWorld(const Layers &layers, const PhysicsSettings &settings)
{
	m_system.Init(
	    settings.maxBodies, 0, settings.maxBodyPairs, settings.maxContactConstraints, layers.broadPhase,
	    *layers.objectVsBroadPhase, layers.pairs);
	// Orbits are on rails and a ship's own thrust moves it: nothing falls.
	m_system.SetGravity(JPH::Vec3::sZero());
}

JPH::BodyID StarSystemWorld::Allocate()
{
	std::uint32_t index = 0;
	if (m_free.empty())
	{
		index = static_cast<std::uint32_t>(m_sequences.size());
		m_sequences.push_back(0);
	}
	else
	{
		index = *m_free.begin();
		m_free.erase(m_free.begin());
	}
	++m_bodyCount;
	return JPH::BodyID{index, m_sequences[index]};
}

void StarSystemWorld::Release(const JPH::BodyID &id)
{
	const std::uint32_t index = id.GetIndex();
	JETTISON_ASSERT(index < m_sequences.size() && !m_free.contains(index));
	// Wraps at 256, as Jolt's own sequence numbers do.
	++m_sequences[index];
	m_free.insert(index);
	--m_bodyCount;
}

void StarSystemWorld::RestoreIdentifiers(
    std::vector<std::uint8_t> sequences, const std::vector<std::uint32_t> &held)
{
	m_sequences = std::move(sequences);
	m_free.clear();
	for (std::uint32_t index = 0; index < m_sequences.size(); ++index)
		m_free.insert(index);
	for (const std::uint32_t index : held)
		m_free.erase(index);
	m_bodyCount = held.size();
}
} // namespace Jettison::Physics::Detail
