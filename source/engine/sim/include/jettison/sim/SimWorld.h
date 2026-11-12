#pragma once

#include <entt/entt.hpp>

#include <algorithm>
#include <any>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <jettison/ecs/Scheduler.h>
#include <jettison/math/Random.h>
#include <jettison/serialise/Binary.h>
#include <jettison/sim/CommandQueue.h>
#include <jettison/sim/Events.h>
#include <jettison/sim/Snapshot.h>
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

// State the world keeps outside its registry, in a service it owns: physics' bodies and their
// contacts, which live in Jolt. A service is saved beside the components, so its state is in the
// save and the hash as theirs is, and a world loaded from a save carries on as the one saved did.
// Its systems are registered with the world like any other, and reach it through what they capture.
class WorldService
{
  public:
	WorldService() = default;
	virtual ~WorldService() = default;
	WorldService(const WorldService &) = delete;
	WorldService &operator=(const WorldService &) = delete;

	// The state, after the world's components. Part of Save, so of StateHash.
	virtual void Save(Serialise::BinaryWriter &writer) const = 0;
	// Reads a save's state against the registry the save is being loaded into, which is complete
	// but not yet the world's, without changing the service. Returns what puts the state in place,
	// which the world calls once that registry is its own and which does not throw. Throws
	// FormatError, through reader.Fail, for state that does not fit, and the world is then as it
	// was.
	virtual std::function<void()> Load(
	    Serialise::BinaryReader &reader, const entt::registry &loaded) = 0;
};

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
// draws from it in an order its step fixes, so the draws are part of the replay. The event queue
// to presentation (Events()) is in the context too, and is not part of the state.
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

	// A component presentation may see: every snapshot taken of the world from now on carries every
	// entity's copy of it (Snapshot::Components), which is how a market reaches a screen without the
	// screen reading the world. Copied at each capture, so a component shown should be small; the
	// order is the entities' identifiers'. Throws std::logic_error for a type shown twice.
	template <typename T> void ShowInSnapshots()
	{
		AddShown(
		    entt::type_hash<T>::value(),
		    [](const entt::registry &registry, std::any &samples)
		    {
			    std::vector<ComponentSample<T>> copied;
			    for (const auto [entity, value] : registry.view<const T>().each())
				    copied.push_back({entity, value});
			    std::ranges::sort(
			        copied, {},
			        [](const ComponentSample<T> &sample) { return entt::to_integral(sample.entity); });
			    samples = std::move(copied);
		    });
	}
	// What the snapshots copy, by EnTT's type hash: Capture runs each.
	using ShownCapture = std::function<void(const entt::registry &, std::any &)>;
	const std::vector<std::pair<entt::id_type, ShownCapture>> &Shown() const
	{
		return m_shown;
	}

	// A system for the simulation phase; throws std::logic_error for one in another phase, and
	// as Ecs::Scheduler::Register does.
	void RegisterSystem(Ecs::SystemDescription system);
	// The systems registered, and the order a step runs them in.
	const Ecs::Scheduler &Scheduler() const
	{
		return m_scheduler;
	}

	// A service whose state the world saves, held by the world from now on, under a name a save
	// knows it by. Throws std::logic_error for a name already registered.
	WorldService &AddService(std::string name, std::unique_ptr<WorldService> service);

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
	// How fast the world runs, which is the player's to choose and not part of the state: a save
	// leaves them out, and a load keeps the ones the world had. The clock throws for a factor that
	// is not positive and finite.
	void SetPaused(bool paused)
	{
		m_clock.SetPaused(paused);
	}
	void SetCompression(double factor)
	{
		m_clock.SetCompression(factor);
	}
	double FixedStep() const
	{
		return m_fixedStep;
	}
	Math::Random &Random();
	EventQueue &Events();

	// One fixed step: apply the queued commands in order, run the simulation-phase systems, and
	// advance the clock. Throws std::logic_error, before applying any, if a queued command has
	// no registered handler. An exception from a command or a system leaves the world part-way
	// through a step, from which only a load recovers.
	void Step();

	// A command the simulation's own agents send as a system runs, a trader's behaviour among them
	// (Ai::Behaviours): applied at once by its handler, as the queue's are at the step's start, so it
	// meets the same checks a player's does. Not recorded, since it is the state's own consequence,
	// which a replay from the same start sends again; recorded, it would be applied twice. Throws
	// std::logic_error for a command with no handler.
	template <typename T> void Apply(const T &command)
	{
		Apply(entt::forward_as_meta(command));
	}
	void Apply(const entt::meta_any &command);

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

	// The whole state: the clock, the generator, the commands still queued, the entities, every
	// registered component and every service. Little-endian, in the binary archive's layout.
	std::vector<std::byte> Save() const;
	// Replaces the state with a save's. The registrations stay. A save of an earlier version is
	// migrated to the current one first, by the world's chain of steps, one a version; a version 1
	// save (before phase 7a) has no services' state, and loads only into a world with none. Throws
	// FormatError or FormatVersionError for a save that does not fit, among them one without the
	// state of every service the world has, and the world is then as it was before.
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
	using Applier = std::function<void(SimWorld &, const entt::meta_any &)>;
	struct Service
	{
		std::string name;
		std::unique_ptr<WorldService> service;
	};

	void AddComponent(const entt::meta_type &type, Component component);
	void AddShown(entt::id_type type, ShownCapture capture);
	void AddCommand(const entt::meta_type &type, Applier apply);

	double m_fixedStep;
	entt::registry m_registry;
	CommandQueue m_commands;
	Time::SimClock m_clock;
	Ecs::Scheduler m_scheduler;
	// Saved in registration order, which the game keeps the same from run to run.
	std::vector<Component> m_components;
	// Keyed by EnTT's type ID; looked up, never iterated, so no outcome depends on its order.
	std::unordered_map<entt::id_type, Applier> m_applies;
	// In the order they were shown; each fills its own entry of a snapshot.
	std::vector<std::pair<entt::id_type, ShownCapture>> m_shown;
	bool m_recording{false};
	std::vector<AppliedCommand> m_recorded;
	// Saved in the order they were added. After the registry, so they go before it: a service's
	// destructor may still look at the entities its state names.
	std::vector<Service> m_services;
};
} // namespace Jettison::Sim
