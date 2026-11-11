#pragma once

#include <optional>

#include <glm/vec2.hpp>

#include <jettison/input/Actions.h>

struct GLFWwindow;

namespace Jettison::App
{
// Feeds an InputSystem from GLFW by polling, once per frame, every input a context binds: keys,
// mouse buttons, the first gamepad, every joystick's axes and buttons, and the cursor's motion.
// Polling rather than callbacks leaves GLFW's callbacks to the ImGui backend, which installs its
// own; the cost is that a tap shorter than a frame is missed, which at the frame rates the shell
// runs is shorter than a key press. The wheel has no state to poll, so it is the one callback:
// installed over the ImGui backend's, which it hands every scroll on to, and summed until the feed.
//
// One window at a time: GLFW's callbacks are free functions, and the window's user pointer is the
// renderer's.
class GlfwInput
{
  public:
	GlfwInput() = default;
	GlfwInput(const GlfwInput &) = delete;
	GlfwInput &operator=(const GlfwInput &) = delete;
	~GlfwInput();

	// After the ImGui backend has installed its callbacks, so the wheel's comes first and passes on
	// to it. Detach puts the backend's back, before the window goes.
	void Attach(GLFWwindow *window);
	void Detach();

	// mouseCaptured is the UI's claim on the mouse (ImGui's WantCaptureMouse): the wheel scrolls a
	// window under the cursor, not the camera behind it.
	void Feed(GLFWwindow *window, Input::InputSystem &input, bool mouseCaptured = false);

  private:
	static void OnScroll(GLFWwindow *window, double x, double y);

	GLFWwindow *m_pWindow{nullptr};
	// Where the cursor was at the last feed; none before the first, so it starts with no motion.
	std::optional<glm::dvec2> m_cursor;
	// The wheel's steps since the last feed.
	glm::dvec2 m_wheel{0.0};
};
} // namespace Jettison::App
