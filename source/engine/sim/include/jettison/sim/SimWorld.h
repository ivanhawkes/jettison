#pragma once

#include <entt/entt.hpp>

#include <cstdint>

#include <jettison/sim/CommandQueue.h>
#include <jettison/time/Clock.h>

namespace Jettison::Sim
{
// The authoritative world: its own registry, advanced in fixed steps on simulation time,
// deterministic across runs and across Debug and Release (docs/coding-standards.md, "Simulation
// code"). Everything a save contains lives here. Presentation never holds a reference into it;
// it reads snapshots and events. Filled in phase 3; the headless replay test is its exit test.
class SimWorld
{
  public:
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

	// One fixed step: apply the queued commands, run the simulation-phase systems, advance the
	// clock.
	void Step(double fixedStep);

	// A hash of the whole state, for the replay test: the same seed and command stream must give
	// the same hash in every build.
	std::uint64_t StateHash() const;

  private:
	entt::registry m_registry;
	CommandQueue m_commands;
	Time::SimClock m_clock;
};
} // namespace Jettison::Sim
