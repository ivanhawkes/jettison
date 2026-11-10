#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <jettison/ecs/Scheduler.h>
#include <jettison/math/Random.h>
#include <jettison/serialise/Binary.h>
#include <jettison/sim/CommandQueue.h>
#include <jettison/time/Clock.h>

namespace Jettison::Sim
{
namespace Detail
{
// EnTT's snapshot and snapshot loader call an archive with entity counts, entities and
// components; these hand each to the binary streams. Through the snapshot, the save keeps the
// entity storage as it was (packed order, free list, the next identifier) and every component
// storage in its packed order, so a loaded world creates the same entities and its views visit
// them in the same order as the world that was saved.
struct SnapshotOutput
{
	Serialise::BinaryWriter &writer;

	void operator()(std::uint32_t count)
	{
		writer.WriteUnsigned(count, 4);
	}
	void operator()(entt::entity entity)
	{
		writer.Write(entity);
	}
	template <typename T> void operator()(const T &component)
	{
		writer.Write(component);
	}
};

struct SnapshotInput
{
	Serialise::BinaryReader &reader;

	// Every counted item is at least an entity's four bytes, so a count the data cannot hold is
	// refused before the loader reserves storage for it.
	void operator()(std::uint32_t &count)
	{
		count = static_cast<std::uint32_t>(reader.ReadUnsigned(4));
		if (count > reader.Remaining() / 4)
			reader.Fail("the count is more than the data holds");
	}
	void operator()(entt::entity &entity)
	{
		reader.Read(entity);
	}
	template <typename T> void operator()(T &component)
	{
		reader.Read(component);
	}
};

// The name Reflect registered a type under; throws std::logic_error if it has none.
std::string_view ReflectedName(const entt::meta_type &type, std::string_view what);
} // namespace Detail

// The authoritative world: its own registry, advanced in fixed steps on simulation time,
// deterministic across runs and across Debug and Release (docs/coding-standards.md, "Simulation
// code"). Everything a save contains lives here. Presentation never holds a reference into it; it
// reads snapshots and events.
//
// The game registers, at start-up and in the same order every run, the component types the world
// holds, the commands it accepts and the systems a step runs. Components and commands are classes
// registered with Reflect; a component's Saved fields are what a save keeps, so a component holds
// no state outside them that the simulation depends on.
//
// The world's generator is in the registry's context (Random()), where systems reach it. A system
// draws from it in an order its step fixes, so the draws are part of the replay.
class SimWorld
{
  public:
	// Throws std::invalid_argument for a step that is not positive and finite.
	SimWorld(double fixedStep, std::uint64_t seed);

	// A component type the world saves and hashes. Throws std::logic_error for a type Reflect
	// does not know or one registered twice.
	template <typename T> void RegisterComponent()
	{
		AddComponent(
		    entt::resolve<T>(), {[](const entt::registry &registry, Serialise::BinaryWriter &writer)
		                         {
			                         Detail::SnapshotOutput output{writer};
			                         entt::snapshot{registry}.get<T>(output);
		                         },
		                         [](entt::snapshot_loader &loader, Serialise::BinaryReader &reader)
		                         {
			                         Detail::SnapshotInput input{reader};
			                         loader.get<T>(input);
		                         },
		                         {}});
	}

	// How a command is applied. Throws std::logic_error for a type Reflect does not know or one
	// registered twice.
	template <typename T> void RegisterCommand(std::function<void(SimWorld &, const T &)> apply)
	{
		AddCommand(
		    entt::resolve<T>(),
		    [apply = std::move(apply)](SimWorld &world, const entt::meta_any &command)
		    { apply(world, command.cast<const T &>()); });
	}

	// A system for the simulation phase; throws std::logic_error for one in another phase, and
	// as Ecs::Scheduler::Register does.
	void RegisterSystem(Ecs::SystemDescription system);

	entt::registry &Registry()
	{
		return m_registry;
	}
	const entt::registry &Registry() const
	{
		return m_registry;
	}
	CommandQueue &Commands()
	{
		return m_commands;
	}
	const Time::SimClock &Clock() const
	{
		return m_clock;
	}
	double FixedStep() const
	{
		return m_fixedStep;
	}
	Math::Random &Random();

	// One fixed step: apply the queued commands in order, run the simulation-phase systems, and
	// advance the clock. Throws std::logic_error, before applying any, if a queued command has
	// no registered handler. An exception from a command or a system leaves the world part-way
	// through a step, from which only a load recovers.
	void Step();

	// While recording, every applied command is kept with the step it was applied at: fed back to
	// a world loaded from the same start, they replay the session.
	void SetRecording(bool recording)
	{
		m_recording = recording;
	}
	const std::vector<AppliedCommand> &Recorded() const
	{
		return m_recorded;
	}

	// The whole state: the clock, the generator, the commands still queued, the entities and
	// every registered component. Little-endian, in the binary archive's layout.
	std::vector<std::byte> Save() const;
	// Replaces the state with a save's. The registrations stay. Throws FormatError or
	// FormatVersionError for a save that does not fit, and the world is then as it was before.
	void Load(std::span<const std::byte> save);

	// A hash of the save: the same seed and command stream give the same hash in every build,
	// and two worlds with the same hash have the same state.
	std::uint64_t StateHash() const;

  private:
	struct Component
	{
		std::function<void(const entt::registry &, Serialise::BinaryWriter &)> save;
		std::function<void(entt::snapshot_loader &, Serialise::BinaryReader &)> load;
		std::string name;
	};
	using Apply = std::function<void(SimWorld &, const entt::meta_any &)>;

	void AddComponent(const entt::meta_type &type, Component component);
	void AddCommand(const entt::meta_type &type, Apply apply);

	double m_fixedStep;
	entt::registry m_registry;
	CommandQueue m_commands;
	Time::SimClock m_clock;
	Ecs::Scheduler m_scheduler;
	// Saved in registration order, which the game keeps the same from run to run.
	std::vector<Component> m_components;
	// Keyed by EnTT's type ID; looked up, never iterated, so no outcome depends on its order.
	std::unordered_map<entt::id_type, Apply> m_applies;
	bool m_recording{false};
	std::vector<AppliedCommand> m_recorded;
};
} // namespace Jettison::Sim
