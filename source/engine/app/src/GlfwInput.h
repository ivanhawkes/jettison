#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>

#include <jettison/input/Actions.h>
#include <jettison/input/Joysticks.h>

struct GLFWwindow;

namespace Jettison::App
{
// Feeds an InputSystem from GLFW by polling, once per frame, every input a context binds: keys,
// mouse buttons, the first gamepad, every joystick's axes and buttons (each binding's joystick the
// device a JoystickMap says it is), and the cursor's motion.
// Polling rather than callbacks for the actions; the cost is that a tap shorter than a frame is
// missed, which at the frame rates the shell runs is shorter than a key press. The wheel has no state
// to poll, and the UI wants each key, button and character in order, so those are callbacks,
// installed over the ImGui backend's, which they hand every event on to.
//
// One window at a time: GLFW's callbacks are free functions, and the window's user pointer is the
// renderer's.
// What the window received as events since the last take, in order, for the UI: keys and buttons as
// they went down and up, text typed, and the wheel, which polling would miss or merge.
struct WindowEvent
{
	enum class Kind : std::uint8_t
	{
		Key,
		MouseButton,
		Text,
		Wheel,
	};
	Kind kind{Kind::Key};
	// GLFW's key or button number, and whether it went down.
	int code{0};
	bool pressed{false};
	bool shift{false};
	bool control{false};
	bool alt{false};
	char32_t character{0};
	float wheel{0.0f};
};

class GlfwInput
{
  public:
	GlfwInput() = default;
	GlfwInput(const GlfwInput &) = delete;
	GlfwInput &operator=(const GlfwInput &) = delete;
	~GlfwInput();

	// After the ImGui backend has installed its callbacks, so the wheel's, the keys', the buttons' and
	// the text's come first and pass on to it. Detach puts the backend's back, before the window goes.
	void Attach(GLFWwindow *window);
	void Detach();

	// Which device each binding's joystick number reads (Input::JoystickMap), from the next feed; with
	// none, a number is GLFW's slot of that number. The map outlives the feeds.
	void SetJoysticks(const Input::JoystickMap *joysticks)
	{
		m_pJoysticks = joysticks;
	}
	// The joysticks plugged in now, by slot, with their GUIDs.
	std::vector<Input::JoystickDevice> ConnectedJoysticks() const;

	// mouseCaptured is the UI's claim on the mouse (ImGui's WantCaptureMouse): the wheel scrolls a
	// window under the cursor, not the camera behind it.
	void Feed(GLFWwindow *window, Input::InputSystem &input, bool mouseCaptured = false);
	// The window's events since the last take (WindowEvent).
	std::vector<WindowEvent> TakeEvents()
	{
		return std::exchange(m_events, {});
	}
	// Where the cursor was at the last feed, in window pixels from the top left; none before the
	// first.
	std::optional<glm::dvec2> Cursor() const
	{
		return m_cursor;
	}

  private:
	static void OnScroll(GLFWwindow *window, double x, double y);
	static void OnKey(GLFWwindow *window, int key, int scancode, int action, int mods);
	static void OnMouseButton(GLFWwindow *window, int button, int action, int mods);
	static void OnChar(GLFWwindow *window, unsigned int codepoint);

	GLFWwindow *m_pWindow{nullptr};
	const Input::JoystickMap *m_pJoysticks{nullptr};
	// Where the cursor was at the last feed; none before the first, so it starts with no motion.
	std::optional<glm::dvec2> m_cursor;
	// The wheel's steps since the last feed.
	glm::dvec2 m_wheel{0.0};
	std::vector<WindowEvent> m_events;
};
} // namespace Jettison::App
