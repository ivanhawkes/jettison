#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Jettison::Ecs
{
// The frame's phases, in order (docs/architecture.md, "Frame and tick structure"). Simulation
// runs zero or more fixed steps per frame; the rest run once.
enum class TickPhase : std::uint8_t
{
	Input,
	Commands,
	Simulation,
	Sync,
	Presentation,
	Render
};

// What a system touches, declared up front, by component type: Access::Of<Reads...>() with
// .Writing<Writes...>(). It does not change the order systems run in today; it is what the job
// system (deferred, docs/architecture.md, "Threading") will read to run systems in parallel, and
// what Scheduler::Ambiguities reports on now.
struct Access
{
	std::vector<entt::id_type> reads;
	std::vector<entt::id_type> writes;

	template <typename... Components> static Access Of()
	{
		return {{entt::type_hash<Components>::value()...}, {}};
	}
	template <typename... Components> Access Writing() &&
	{
		(writes.push_back(entt::type_hash<Components>::value()), ...);
		return std::move(*this);
	}

	// Whether running the two in either order could give different results: one writes what
	// the other reads or writes.
	bool ConflictsWith(const Access &other) const;
};

struct SystemDescription
{
	std::string name;
	TickPhase phase{TickPhase::Presentation};
	Access access;
	std::vector<std::string> after; // Names of systems this one runs after, within its phase.
	std::function<void(entt::registry &, double deltaTime)> run;
};

// Registers systems and runs each phase's systems in a valid order: after every system it names
// in after, and otherwise in registration order, so the order is the same every run. Registration
// happens at start-up.
//
// A system's after may name one registered later. Register throws std::logic_error for a system
// with no name or no run, a name already registered, an after naming a system in another phase,
// and a cycle, as soon as the system that closes it is registered. Validate() throws for an after
// that names no system at all; Run calls it the first time it runs, but the app calls it after
// registration so the error comes at start-up.
class Scheduler
{
  public:
	void Register(SystemDescription system);
	void Validate() const;
	// Runs the phase's systems in order. An exception from a system propagates, and the systems
	// after it do not run.
	void Run(TickPhase phase, entt::registry &registry, double deltaTime) const;

	const std::vector<SystemDescription> &Systems() const
	{
		return m_systems;
	}
	// The phase's systems in the order Run runs them.
	std::vector<std::string> Order(TickPhase phase) const;
	// Pairs of systems in the phase whose access conflicts and which no chain of after
	// constraints orders: registration order decides theirs today, and they are what a parallel
	// scheduler would have to keep apart. Each pair is in run order.
	std::vector<std::pair<std::string, std::string>> Ambiguities(TickPhase phase) const;

  private:
	// Indices into m_systems, for one phase, in run order; throws for a cycle.
	std::vector<std::size_t> Sort(TickPhase phase) const;

	std::vector<SystemDescription> m_systems;
	// Run order per phase, rebuilt by Register.
	std::vector<std::vector<std::size_t>> m_order;
	mutable bool m_validated{false};
};
} // namespace Jettison::Ecs
