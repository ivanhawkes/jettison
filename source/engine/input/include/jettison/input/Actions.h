#pragma once

#include <glm/vec2.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <jettison/input/InputCode.h>

namespace Jettison::Core
{
class Config;
}

namespace Jettison::Input
{
// An action is what the game asks about: "throttle up", "select", "map pan". Bindings map
// device inputs to actions. Contexts (flight, map, UI, console) are pushed and popped; the top
// context's bindings win.
enum class ActionType : std::uint8_t
{
	Button, // Pressed, held, released.
	Axis,   // -1 to 1.
	Vector  // A 2D axis pair: mouse motion, a stick.
};

// An action as the game holds it: looked up by name once, at start-up, and queried every frame
// without a string compare.
struct ActionId
{
	std::uint32_t index{0};

	friend bool operator==(const ActionId &, const ActionId &) = default;
};

// Main thread only.
//
// The platform layer feeds raw input (SetButton, SetAxis, AddAxisDelta) as it arrives, and Poll()
// turns it into the frame's actions. A context higher on the stack claims the inputs it binds: a
// lower context does not see them, so the map's W can pan while the flight context under it binds
// W to throttle. An opaque context, the console, hides everything below it.
//
// A context's bindings are its defaults (Bind) unless they are overridden (Rebind, or
// LoadBindings from the user's settings); SaveBindings writes only the overrides, so a default
// changed in a later version still reaches a player who did not touch it.
//
// Action and context names are letters, digits and underscores, since they are segments of a
// settings key, "input.flight.throttle". Every setup call throws std::logic_error for a name
// that is not valid, defined twice, or not defined.
class InputSystem
{
  public:
	ActionId DefineAction(std::string name, ActionType type);
	ActionId Action(std::string_view name) const;
	void DefineContext(std::string name, bool opaque = false);

	// Throws std::logic_error for a binding that does not suit the action's type: a component
	// other than 0 on anything but a vector, or above 1 on one.
	void Bind(std::string_view context, ActionId action, Binding binding);
	void Rebind(std::string_view context, ActionId action, std::vector<Binding> bindings);
	// Drops the override, back to the defaults.
	void ResetBindings(std::string_view context, ActionId action);
	const std::vector<Binding> &Bindings(std::string_view context, ActionId action) const;

	void PushContext(std::string_view name);
	// Throws std::logic_error on an empty stack.
	void PopContext();
	std::size_t ContextDepth() const
	{
		return m_stack.size();
	}

	// Raw input. A button pressed and released between two polls counts as pressed for one
	// frame, so a tap shorter than a frame is not lost.
	void SetButton(InputCode input, bool down);
	// An absolute reading, a stick's or a trigger's.
	void SetAxis(InputCode input, float value);
	// Relative motion, the mouse's, summed until the next poll.
	void AddAxisDelta(InputCode input, float delta);
	// Every input any context binds, sorted, for a source that polls rather than listens.
	std::vector<InputCode> BoundInputs() const;
	// Every input the overrides bind, sorted: the ones the player chose.
	std::vector<InputCode> OverriddenInputs() const;

	// Once per frame, before the Input phase's systems read the actions.
	void Poll();
	bool IsPressed(ActionId action) const;
	bool WasPressedThisFrame(ActionId action) const;
	bool WasReleasedThisFrame(ActionId action) const;
	// A button reads 1 while pressed; a vector's axis is its x.
	float Axis(ActionId action) const;
	glm::vec2 Vector(ActionId action) const;

	// Under "input.<context>.<action>", in the user layer, as FormatBindings writes them; an action
	// with no override is taken out of the layer, so one reset to its defaults stays reset.
	void SaveBindings(Core::Config &config) const;
	// Takes every "input.<context>.<action>" the config has as an override. Throws
	// std::runtime_error naming the key for text that is not bindings; nothing is applied then.
	void LoadBindings(const Core::Config &config);

  private:
	struct ActionState
	{
		std::string name;
		ActionType type;
		glm::vec2 value{0.0f};
		bool pressed{false};
		bool wasPressed{false};
	};
	struct Context
	{
		std::string name;
		bool opaque{false};
		// By action index; ordered so Poll and SaveBindings walk them the same way every time.
		std::map<std::uint32_t, std::vector<Binding>> defaults;
		std::map<std::uint32_t, std::vector<Binding>> overrides;
	};
	struct Raw
	{
		float value{0.0f};
		float delta{0.0f};
		bool latched{false};
	};

	Context &ContextNamed(std::string_view name);
	const Context &ContextNamed(std::string_view name) const;
	const ActionState &State(ActionId action) const;
	void Check(ActionId action, const Binding &binding) const;
	float Read(InputCode input, float deadZone) const;

	std::vector<ActionState> m_actions;
	std::vector<Context> m_contexts;
	// Indices into m_contexts, bottom first.
	std::vector<std::size_t> m_stack;
	std::map<InputCode, Raw> m_raw;
};
} // namespace Jettison::Input
