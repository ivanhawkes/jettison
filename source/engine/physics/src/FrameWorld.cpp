#include "FrameWorld.h"

#include <utility>

#include <jettison/core/Assert.h>

namespace Jettison::Physics::Detail
{
namespace
{
// Jolt's broadphase keeps static bodies apart from moving ones, which is what lets it skip the static
// tree for pairs, whatever layers they are in.
constexpr JPH::BroadPhaseLayer kStaticBroadPhase{0};
constexpr JPH::BroadPhaseLayer kMovingBroadPhase{1};
constexpr JPH::uint kBroadPhaseLayers = 2;
} // namespace


Layers::Layers(std::uint32_t count, std::span<const std::pair<std::uint32_t, std::uint32_t>> meetings)
    : pairs{kPerLayer * count}, broadPhase{kPerLayer * count, kBroadPhaseLayers}
{
	// Jolt's table is symmetric: enabling one way enables both.
	const auto meet = [this](std::uint32_t first, std::uint32_t second)
	{
		pairs.EnableCollision(Moving(first), Static(second));
		pairs.EnableCollision(Moving(second), Static(first));
		pairs.EnableCollision(Moving(first), Moving(second));
	};
	for (std::uint32_t layer = 0; layer < count; ++layer)
	{
		meet(kNone, layer);
		broadPhase.MapObjectToBroadPhaseLayer(Static(layer), kStaticBroadPhase);
		broadPhase.MapObjectToBroadPhaseLayer(Moving(layer), kMovingBroadPhase);
	}
	for (const auto &[first, second] : meetings)
		meet(first, second);
	objectVsBroadPhase.emplace(broadPhase, kBroadPhaseLayers, pairs, kPerLayer * count);
}


FrameWorld::FrameWorld(const Layers &layers, const PhysicsSettings &settings)
{
	m_system.Init(
	    settings.maxBodies, 0, settings.maxBodyPairs, settings.maxContactConstraints, layers.broadPhase,
	    *layers.objectVsBroadPhase, layers.pairs);

	// Orbits are on rails and a vehicle's own thrust moves it: nothing falls.
	m_system.SetGravity(JPH::Vec3::sZero());
}


JPH::BodyID FrameWorld::Allocate()
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


void FrameWorld::Release(const JPH::BodyID &id)
{
	const std::uint32_t index = id.GetIndex();
	JETTISON_ASSERT(index < m_sequences.size() && !m_free.contains(index));

	// Wraps at 256, as Jolt's own sequence numbers do.
	++m_sequences[index];
	m_free.insert(index);
	--m_bodyCount;
}


void FrameWorld::RestoreIdentifiers(
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
