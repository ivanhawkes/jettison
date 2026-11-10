#pragma once

#include <optional>

namespace Jettison::Ecs
{
// A small finite state machine for ship states and application flow (menu, loading, in system,
// jumping): a current state and a transition table, with a hook on entry and exit. Transitions
// are data added at construction, so a ship's states live in the game and the engine runs them.
// Filled in phase 3.
template <typename State, typename Event> class StateMachine
{
  public:
	explicit StateMachine(State initial) : m_state{initial}
	{
	}

	State Current() const
	{
		return m_state;
	}

	void AddTransition(State from, Event event, State to);
	// The new state, or nothing when the event has no transition from the current state.
	std::optional<State> Dispatch(Event event);

  private:
	State m_state;
};
} // namespace Jettison::Ecs
