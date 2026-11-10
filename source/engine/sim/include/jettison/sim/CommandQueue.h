#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace Jettison::Sim
{
// A command from presentation to the simulation: "buy 40 units at 1,200", "set throttle". The
// game defines the concrete commands; the engine carries them, stamps the step they are applied
// on, and records them for replays. Commands are applied at the start of a step, in the order
// they were queued.
class Command
{
  public:
	virtual ~Command() = default;
	std::uint64_t appliedOnStep{0};
};

// The seam between presentation and simulation, and the seam for replays, automated tests and
// any later networking (docs/architecture.md, "Simulation and presentation"). Filled in phase 3.
class CommandQueue
{
  public:
	void Push(std::unique_ptr<Command> command);
	// Everything queued since the last Drain, in order, for the step about to run.
	std::vector<std::unique_ptr<Command>> Drain();
	std::size_t Pending() const
	{
		return m_pending.size();
	}

  private:
	std::vector<std::unique_ptr<Command>> m_pending;
};
} // namespace Jettison::Sim
