#pragma once

#include <entt/entt.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace Jettison::Sim
{
class SimWorld;

// An event the simulation raised for presentation: "docked", "trade filled", "contact". The step
// it was raised in, and the event, a plain struct the game defines.
struct SimEvent
{
	std::uint64_t step{0};
	entt::meta_any event;
};

// Events flow one way, from the simulation to presentation, which turns them into sound, effects
// and UI (docs/architecture.md, "Simulation and presentation"). A command handler or a system
// raises one with Emit; presentation takes the lot once a frame, in the order they were raised, and
// tells them apart with meta_any::try_cast. One queue, not a queue per type as EnTT's dispatcher
// keeps, so presentation sees "docked" before the "trade filled" that came after it.
//
// The queue lives in the world's registry context, beside the generator, so a system reaches it
// with registry.ctx().get<Sim::EventQueue>(). It is not part of the state: a save leaves it out, a
// load empties it, and nothing in the simulation may read it back, since what presentation has
// taken is presentation's business.
class EventQueue
{
  public:
	template <typename T> void Emit(T event)
	{
		m_events.push_back({m_step, entt::meta_any{std::move(event)}});
	}

	// Everything raised since the last Take, in order.
	std::vector<SimEvent> Take()
	{
		return std::exchange(m_events, {});
	}
	const std::vector<SimEvent> &Pending() const
	{
		return m_events;
	}

  private:
	friend class SimWorld;

	// The step being run, which SimWorld::Step sets before it applies the step's commands.
	std::uint64_t m_step{0};
	std::vector<SimEvent> m_events;
};
} // namespace Jettison::Sim
