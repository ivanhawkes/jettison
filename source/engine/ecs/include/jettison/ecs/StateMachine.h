#pragma once

#include <algorithm>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Jettison::Ecs
{
// A small finite state machine for ship states and application flow (menu, loading, in system,
// jumping): a current state and a transition table, with a hook on entry and exit. Transitions
// are data added at construction, so a ship's states live in the game and the engine runs them.
//
// State and Event need only ==: the tables are searched in order, which suits the handful of
// entries a machine has and keeps the machine deterministic in simulation code. A transition to
// the state it starts from is a transition: the exit and entry hooks run.
template <typename State, typename Event> class StateMachine
{
  public:
	using Hook = std::function<void()>;

	explicit StateMachine(State initial) : m_state{initial}
	{
	}

	State Current() const
	{
		return m_state;
	}

	// Throws std::logic_error for a second transition on the same event from the same state: a
	// table with two answers has none.
	void AddTransition(State from, Event event, State to)
	{
		if (Find(from, event))
			throw std::logic_error{"StateMachine: the state already has a transition on that event"};
		m_transitions.push_back({from, event, to});
	}

	// Each runs when the machine enters or leaves the state through Dispatch; not for the initial
	// state at construction. A state may have one of each; a second replaces the first.
	void OnEnter(State state, Hook hook)
	{
		Set(m_enter, state, std::move(hook));
	}
	void OnExit(State state, Hook hook)
	{
		Set(m_exit, state, std::move(hook));
	}

	// Whether the event has a transition from the current state.
	bool CanDispatch(Event event) const
	{
		return Find(m_state, event) != nullptr;
	}

	// The new state, or nothing when the event has no transition from the current state. Runs the
	// old state's exit hook, changes state, then runs the new state's entry hook. Throws
	// std::logic_error if called from a hook: a hook that wants a transition returns, and its
	// owner dispatches the next event.
	std::optional<State> Dispatch(Event event)
	{
		if (m_dispatching)
			throw std::logic_error{"StateMachine: Dispatch was called from a hook"};
		const Transition *transition = Find(m_state, event);
		if (!transition)
			return std::nullopt;

		const State to = transition->to;
		m_dispatching = true;
		// Cleared however the hooks leave, so a hook that throws does not lock the machine.
		const struct Reset
		{
			bool &flag;
			~Reset()
			{
				flag = false;
			}
		} reset{m_dispatching};
		Run(m_exit, m_state);
		m_state = to;
		Run(m_enter, m_state);
		return to;
	}

  private:
	struct Transition
	{
		State from;
		Event event;
		State to;
	};

	const Transition *Find(const State &from, const Event &event) const
	{
		const auto found = std::ranges::find_if(
		    m_transitions, [&](const Transition &transition)
		    { return transition.from == from && transition.event == event; });
		return found == m_transitions.end() ? nullptr : &*found;
	}

	static void Set(std::vector<std::pair<State, Hook>> &hooks, const State &state, Hook hook)
	{
		const auto found =
		    std::ranges::find_if(hooks, [&](const auto &entry) { return entry.first == state; });
		if (found != hooks.end())
			found->second = std::move(hook);
		else
			hooks.emplace_back(state, std::move(hook));
	}

	static void Run(const std::vector<std::pair<State, Hook>> &hooks, const State &state)
	{
		const auto found =
		    std::ranges::find_if(hooks, [&](const auto &entry) { return entry.first == state; });
		if (found != hooks.end() && found->second)
			found->second();
	}

	State m_state;
	std::vector<Transition> m_transitions;
	std::vector<std::pair<State, Hook>> m_enter;
	std::vector<std::pair<State, Hook>> m_exit;
	bool m_dispatching{false};
};
} // namespace Jettison::Ecs
