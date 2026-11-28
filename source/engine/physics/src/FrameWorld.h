#pragma once

#include <Jolt/Jolt.h>
// Jolt.h has to come first.
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <jettison/physics/PhysicsWorld.h>

namespace Jettison::Physics::Detail
{
// The object layers every frame's world shares: for each of the game's layers (LayerDefinition),
// numbered from 1, and for no layer, 0, one for its static bodies and one for its moving ones,
// kinematic and dynamic. Static bodies never meet each other; a moving body meets the static and
// moving bodies of each layer its own meets, and no layer meets every layer. Jolt keeps references to
// them, so they outlive every world made with them.
class Layers
{
  public:
	// Each layer's static and moving object layers.
	static constexpr JPH::ObjectLayer kPerLayer = 2;

	// What a body of no layer is in.
	static constexpr std::uint32_t kNone = 0;

	// The game's layers number count less one, and each pair meets either way.
	Layers(std::uint32_t count, std::span<const std::pair<std::uint32_t, std::uint32_t>> meetings);

	// No layers of the game's.
	Layers() : Layers(kNone + 1, {})
	{
	}

	Layers(const Layers &) = delete;

	Layers &operator=(const Layers &) = delete;

	static JPH::ObjectLayer Static(std::uint32_t layer)
	{
		return static_cast<JPH::ObjectLayer>(kPerLayer * layer);
	}

	static JPH::ObjectLayer Moving(std::uint32_t layer)
	{
		return static_cast<JPH::ObjectLayer>(kPerLayer * layer + 1);
	}

	JPH::ObjectLayerPairFilterTable pairs;
	JPH::BroadPhaseLayerInterfaceTable broadPhase;

	// Made from the two above once they are filled in: it tabulates them when it is constructed.
	std::optional<JPH::ObjectVsBroadPhaseLayerFilterTable> objectVsBroadPhase;
};

// One frame's bodies in one local frame: a Jolt world, the identifiers its bodies are given, and
// where its frame is. Jolt's positions are metres from the frame's origin and its velocities are
// relative to the frame's motion (PhysicsWorld.h, "frames").
//
// Jolt hands out body identifiers from a free list whose order is its history, which a save does
// not hold, so a world loaded from a save would give the next body another identifier than the
// world saved did, and Jolt's results depend on them. Bodies are therefore made with identifiers
// chosen here: the lowest index free, with a sequence number per index that goes up each time the
// index is freed, so a body made where one was just removed is not taken for it by the contacts
// the last step left. The sequence numbers are saved; the free indices are what no body has.
class FrameWorld
{
  public:
	FrameWorld(const Layers &layers, const PhysicsSettings &settings);

	FrameWorld(const FrameWorld &) = delete;

	FrameWorld &operator=(const FrameWorld &) = delete;

	JPH::PhysicsSystem &System()
	{
		return m_system;
	}

	const JPH::PhysicsSystem &System() const
	{
		return m_system;
	}

	// Without locks: the simulation has one thread.
	JPH::BodyInterface &Bodies()
	{
		return m_system.GetBodyInterfaceNoLock();
	}

	const JPH::BodyInterface &Bodies() const
	{
		return m_system.GetBodyInterfaceNoLock();
	}

	// The identifier the next body takes, held for it.
	JPH::BodyID Allocate();

	void Release(const JPH::BodyID &id);

	const std::vector<std::uint8_t> &Sequences() const
	{
		return m_sequences;
	}

	// The identifiers as a save left them: each index's sequence number, and the indices bodies
	// hold, every one of which is below the sequences' count.
	void RestoreIdentifiers(
	    std::vector<std::uint8_t> sequences, const std::vector<std::uint32_t> &held);

	std::size_t BodyCount() const
	{
		return m_bodyCount;
	}

	// The local frame's origin in its frame as the last step ended, and its velocity over that
	// step: what the next step's Jolt coordinates are from, and what its velocities were relative
	// to. Saved, since a velocity changes frame by the difference between two steps' velocities.
	// Zero for the frame's own.
	glm::dvec3 frameOrigin{0.0};
	glm::dvec3 frameVelocity{0.0};

  private:
	JPH::PhysicsSystem m_system;
	std::vector<std::uint8_t> m_sequences;
	std::set<std::uint32_t> m_free;
	std::size_t m_bodyCount{0};
};
} // namespace Jettison::Physics::Detail
