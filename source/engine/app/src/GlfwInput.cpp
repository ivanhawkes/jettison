#include "GlfwInput.h"

#include <GLFW/glfw3.h>

namespace Jettison::App
{
// The input module names inputs by GLFW's numbers without including GLFW; these hold it to them.
static_assert(Input::Keys::Space.code == GLFW_KEY_SPACE);
static_assert(Input::Keys::Escape.code == GLFW_KEY_ESCAPE);
static_assert(Input::Keys::Enter.code == GLFW_KEY_ENTER);
static_assert(Input::Keys::Tab.code == GLFW_KEY_TAB);
static_assert(Input::Keys::Up.code == GLFW_KEY_UP);
static_assert(Input::Keys::GraveAccent.code == GLFW_KEY_GRAVE_ACCENT);
static_assert(Input::Keys::F1.code == GLFW_KEY_F1);
static_assert(GLFW_KEY_F25 == GLFW_KEY_F1 + 24 && GLFW_KEY_KP_0 == 320 && GLFW_KEY_MENU == 348);
static_assert(
    Input::Keys::Character('w').code == GLFW_KEY_W && Input::Keys::Character('7').code == GLFW_KEY_7);
static_assert(GLFW_MOUSE_BUTTON_LEFT == 0 && GLFW_MOUSE_BUTTON_MIDDLE == 2);
static_assert(GLFW_GAMEPAD_BUTTON_A == 0 && GLFW_GAMEPAD_BUTTON_DPAD_LEFT == 14);
static_assert(GLFW_GAMEPAD_AXIS_LEFT_Y == 1 && GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER == 5);

void GlfwInput::Feed(GLFWwindow *window, Input::InputSystem &input)
{
	GLFWgamepadstate gamepad{};
	bool gamepadRead = false;
	bool hasGamepad = false;

	for (const Input::InputCode code : input.BoundInputs())
	{
		switch (code.device)
		{
			case Input::Device::Keyboard:
				// glfwGetKey reports an error for a code outside its range rather than answering.
				if (code.code >= GLFW_KEY_SPACE && code.code <= GLFW_KEY_LAST)
					input.SetButton(code, glfwGetKey(window, code.code) == GLFW_PRESS);
				break;
			case Input::Device::MouseButton:
				if (code.code >= 0 && code.code <= GLFW_MOUSE_BUTTON_LAST)
					input.SetButton(code, glfwGetMouseButton(window, code.code) == GLFW_PRESS);
				break;
			case Input::Device::GamepadButton:
			case Input::Device::GamepadAxis:
			{
				if (!gamepadRead)
				{
					gamepadRead = true;
					hasGamepad = glfwJoystickIsGamepad(GLFW_JOYSTICK_1) == GLFW_TRUE &&
					             glfwGetGamepadState(GLFW_JOYSTICK_1, &gamepad) == GLFW_TRUE;
				}
				// A gamepad unplugged reads as released and centred, not as frozen where it was.
				if (code.device == Input::Device::GamepadButton && code.code >= 0 &&
				    code.code <= GLFW_GAMEPAD_BUTTON_LAST)
					input.SetButton(code, hasGamepad && gamepad.buttons[code.code] == GLFW_PRESS);
				else if (
				    code.device == Input::Device::GamepadAxis && code.code >= 0 &&
				    code.code <= GLFW_GAMEPAD_AXIS_LAST)
					input.SetAxis(code, hasGamepad ? gamepad.axes[code.code] : 0.0f);
				break;
			}
			case Input::Device::MouseAxis: break;
		}
	}

	glm::dvec2 cursor{0.0};
	glfwGetCursorPos(window, &cursor.x, &cursor.y);
	if (m_cursor)
	{
		input.AddAxisDelta({Input::Device::MouseAxis, 0}, static_cast<float>(cursor.x - m_cursor->x));
		input.AddAxisDelta({Input::Device::MouseAxis, 1}, static_cast<float>(cursor.y - m_cursor->y));
	}
	m_cursor = cursor;
}
} // namespace Jettison::App
