#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Jettison::Input
{
// An action is what the game asks about: "throttle up", "select", "map pan". Bindings map
// device inputs (keys, buttons, axes, including joystick and HOTAS through GLFW) to actions,
// and are saved to user settings. Contexts (flight, map, UI, console) are pushed and popped; the
// top context's bindings win. Filled in phase 3; the shell's hot keys move onto it then.
enum class ActionType : std::uint8_t
{
	Button, // Pressed, held, released.
	Axis,   // -1 to 1.
	Vector  // A 2D axis pair: mouse delta, a stick.
};

struct ActionDefinition
{
	std::string name;
	ActionType type{ActionType::Button};
};

class InputSystem
{
  public:
	void DefineAction(ActionDefinition action);
	void PushContext(std::string_view name);
	void PopContext();
	std::size_t ContextDepth() const
	{
		return m_contextDepth;
	}

	// Once per frame, before the Input phase's systems read the actions.
	void Poll();
	bool IsPressed(std::string_view action) const;
	bool WasPressedThisFrame(std::string_view action) const;
	float Axis(std::string_view action) const;
	glm::vec2 Vector(std::string_view action) const;

  private:
	std::vector<ActionDefinition> m_actions;
	std::size_t m_contextDepth{0};
};
} // namespace Jettison::Input
