#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace Jettison::Sim
{
// A command from presentation to the simulation: "buy 40 units at 1,200", "set throttle". The
// game defines each command as a plain struct registered with Reflect, its Saved fields what it
// carries, and registers how the world applies it (SimWorld::RegisterCommand). Being reflected is
// what lets a command be saved with the world, recorded for a replay and, later, sent over a
// network.

// A command as it was applied: the step it was applied at the start of, and the command.
struct AppliedCommand
{
	std::uint64_t step{0};
	entt::meta_any command;
};

// The seam between presentation and simulation, and the seam for replays, automated tests and
// any later networking (docs/architecture.md, "Simulation and presentation"). Commands are applied
// at the start of the next step, in the order they were pushed.
class CommandQueue
{
  public:
	template <typename T> void Push(T command)
	{
		Push(entt::meta_any{std::move(command)});
	}
	// Throws std::logic_error for an empty value. Whether the world can apply it is checked when
	// the step that applies it starts.
	void Push(entt::meta_any command);

	// Everything queued since the last Drain, in order, for the step about to run.
	std::vector<entt::meta_any> Drain();
	const std::vector<entt::meta_any> &Pending() const
	{
		return m_pending;
	}

  private:
	std::vector<entt::meta_any> m_pending;
};
} // namespace Jettison::Sim
