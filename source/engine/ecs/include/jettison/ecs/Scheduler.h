#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <string>
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

// What a system touches, declared up front. The scheduler orders systems within a phase by these
// and, when the job system arrives, runs non-conflicting systems in parallel without a rewrite.
struct Access
{
	std::vector<entt::id_type> reads;
	std::vector<entt::id_type> writes;
};

struct SystemDescription
{
	std::string name;
	TickPhase phase{TickPhase::Presentation};
	Access access;
	std::vector<std::string> after; // Names of systems this one runs after, within its phase.
	std::function<void(entt::registry &, double deltaTime)> run;
};

// Registers systems and runs each phase's systems in a valid order. A cycle in the ordering
// constraints is a registration error, not a run-time surprise. Filled in phase 3.
class Scheduler
{
  public:
	void Register(SystemDescription system);
	void Run(TickPhase phase, entt::registry &registry, double deltaTime) const;
	const std::vector<SystemDescription> &Systems() const
	{
		return m_systems;
	}

  private:
	std::vector<SystemDescription> m_systems;
};
} // namespace Jettison::Ecs
