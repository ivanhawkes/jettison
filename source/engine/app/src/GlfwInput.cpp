#include "GlfwInput.h"

#include <GLFW/glfw3.h>

#include <array>
#include <optional>
#include <span>
#include <vector>

namespace Jettison::App
{
namespace
{
// The one window's wheel: which GlfwInput takes it, and the callback it was installed over.
GlfwInput *g_pWheelTarget = nullptr;
GLFWscrollfun g_previousScroll = nullptr;

// One joystick's raw state, read once a feed: nothing for a slot with no joystick in it.
struct JoystickState
{
	bool read{false};
	std::span<const float> axes;
	std::span<const unsigned char> buttons;
};
} // namespace

// The input module names inputs by GLFW's numbers without including GLFW; these hold it to them.
static_assert(Input::Keys::Space.code == GLFW_KEY_SPACE);
static_assert(Input::Keys::Escape.code == GLFW_KEY_ESCAPE);
static_assert(Input::Keys::Enter.code == GLFW_KEY_ENTER);
static_assert(Input::Keys::Tab.code == GLFW_KEY_TAB);
static_assert(Input::Keys::Up.code == GLFW_KEY_UP && Input::Keys::Down.code == GLFW_KEY_DOWN);
static_assert(Input::Keys::Left.code == GLFW_KEY_LEFT && Input::Keys::Right.code == GLFW_KEY_RIGHT);
static_assert(Input::Keys::GraveAccent.code == GLFW_KEY_GRAVE_ACCENT);
static_assert(Input::Keys::F1.code == GLFW_KEY_F1);
static_assert(Input::Keys::F2.code == GLFW_KEY_F2);
static_assert(Input::Keys::F5.code == GLFW_KEY_F5 && Input::Keys::F9.code == GLFW_KEY_F9);
static_assert(GLFW_KEY_F25 == GLFW_KEY_F1 + 24 && GLFW_KEY_KP_0 == 320 && GLFW_KEY_MENU == 348);
static_assert(
    Input::Keys::Character('w').code == GLFW_KEY_W && Input::Keys::Character('7').code == GLFW_KEY_7);
static_assert(GLFW_MOUSE_BUTTON_LEFT == 0 && GLFW_MOUSE_BUTTON_MIDDLE == 2);
static_assert(GLFW_GAMEPAD_BUTTON_A == 0 && GLFW_GAMEPAD_BUTTON_DPAD_LEFT == 14);
static_assert(GLFW_GAMEPAD_AXIS_LEFT_Y == 1 && GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER == 5);
static_assert(GLFW_JOYSTICK_1 == 0 && GLFW_JOYSTICK_LAST + 1 == Input::kJoystickCount);
static_assert(Input::Keys::LeftShift.code == GLFW_KEY_LEFT_SHIFT);
static_assert(Input::Keys::LeftControl.code == GLFW_KEY_LEFT_CONTROL);

GlfwInput::~GlfwInput()
{
	if (g_pWheelTarget == this)
		g_pWheelTarget = nullptr;
}

void GlfwInput::Attach(GLFWwindow *window)
{
	Detach();
	m_pWindow = window;
	g_pWheelTarget = this;
	g_previousScroll = glfwSetScrollCallback(window, &GlfwInput::OnScroll);
}

void GlfwInput::Detach()
{
	if (!m_pWindow)
		return;
	glfwSetScrollCallback(m_pWindow, g_previousScroll);
	g_previousScroll = nullptr;
	g_pWheelTarget = nullptr;
	m_pWindow = nullptr;
}

void GlfwInput::OnScroll(GLFWwindow *window, double x, double y)
{
	if (g_previousScroll)
		g_previousScroll(window, x, y);
	if (g_pWheelTarget && g_pWheelTarget->m_pWindow == window)
		g_pWheelTarget->m_wheel += glm::dvec2{x, y};
}

std::vector<Input::JoystickDevice> GlfwInput::ConnectedJoysticks() const
{
	std::vector<Input::JoystickDevice> connected;
	for (std::uint8_t slot = 0; slot < Input::kJoystickCount; ++slot)
	{
		const int id = GLFW_JOYSTICK_1 + slot;
		if (glfwJoystickPresent(id) != GLFW_TRUE)
			continue;
		const char *guid = glfwGetJoystickGUID(id);
		connected.push_back({slot, guid ? guid : ""});
	}
	return connected;
}

void GlfwInput::Feed(GLFWwindow *window, Input::InputSystem &input, bool mouseCaptured)
{
	GLFWgamepadstate gamepad{};
	bool gamepadRead = false;
	bool hasGamepad = false;
	std::array<JoystickState, Input::kJoystickCount> joysticks{};
	// Which slot each binding's joystick number reads: the map's answer for the devices plugged in
	// now, or the slot of that number with no map.
	std::array<std::optional<std::uint8_t>, Input::kJoystickCount> slots{};
	if (m_pJoysticks)
		slots = m_pJoysticks->Assign(ConnectedJoysticks());
	else
		for (std::uint8_t number = 0; number < Input::kJoystickCount; ++number)
			slots[number] = number;
	const auto joystick = [&joysticks, &slots](std::uint8_t number) -> const JoystickState &
	{
		JoystickState &state = joysticks[number];
		if (!state.read)
		{
			state.read = true;
			if (!slots[number])
				return state;
			const int id = GLFW_JOYSTICK_1 + *slots[number];
			int axes = 0;
			int buttons = 0;
			// Null, with counts of zero, for a slot with nothing in it.
			const float *axis = glfwGetJoystickAxes(id, &axes);
			const unsigned char *button = glfwGetJoystickButtons(id, &buttons);
			if (axis)
				state.axes = {axis, static_cast<std::size_t>(axes)};
			if (button)
				state.buttons = {button, static_cast<std::size_t>(buttons)};
		}
		return state;
	};

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
			case Input::Device::JoystickAxis:
			case Input::Device::JoystickButton:
			{
				if (code.joystick >= Input::kJoystickCount || code.code < 0)
					break;
				// One unplugged, or an axis or button it does not have, reads centred and released.
				const JoystickState &state = joystick(code.joystick);
				const auto index = static_cast<std::size_t>(code.code);
				if (code.device == Input::Device::JoystickAxis)
					input.SetAxis(code, index < state.axes.size() ? state.axes[index] : 0.0f);
				else
					input.SetButton(
					    code, index < state.buttons.size() && state.buttons[index] == GLFW_PRESS);
				break;
			}
			case Input::Device::MouseAxis: break;
		}
	}

	if (!mouseCaptured)
	{
		input.AddAxisDelta({Input::Device::MouseAxis, 2}, static_cast<float>(m_wheel.x));
		input.AddAxisDelta({Input::Device::MouseAxis, 3}, static_cast<float>(m_wheel.y));
	}
	m_wheel = glm::dvec2{0.0};

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
