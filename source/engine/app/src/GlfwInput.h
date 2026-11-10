#pragma once

#include <optional>

#include <glm/vec2.hpp>

#include <jettison/input/Actions.h>

struct GLFWwindow;

namespace Jettison::App
{
// Feeds an InputSystem from GLFW by polling, once per frame, every input a context binds: keys,
// mouse buttons, the first gamepad, and the cursor's motion. Polling rather than callbacks leaves
// GLFW's callbacks to the ImGui backend, which installs its own; the cost is that a tap shorter
// than a frame is missed, which at the frame rates the shell runs is shorter than a key press.
// The wheel needs a callback, and comes when something binds it.
class GlfwInput
{
  public:
	void Feed(GLFWwindow *window, Input::InputSystem &input);

  private:
	// Where the cursor was at the last feed; none before the first, so it starts with no motion.
	std::optional<glm::dvec2> m_cursor;
};
} // namespace Jettison::App
