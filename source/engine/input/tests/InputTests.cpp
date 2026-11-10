#include <jettison/input/Actions.h>
#include <jettison/input/InputCode.h>

#include <jettison/core/Config.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison::Input;

namespace
{
constexpr InputCode W = Keys::Character('w');
constexpr InputCode S = Keys::Character('s');
constexpr InputCode LeftY{Device::GamepadAxis, 1};
constexpr InputCode MouseX{Device::MouseAxis, 0};
constexpr InputCode MouseY{Device::MouseAxis, 1};

// Flight at the bottom, the map over it, the console over both.
struct Fixture
{
	InputSystem input;
	ActionId throttle = input.DefineAction("throttle", ActionType::Axis);
	ActionId fire = input.DefineAction("fire", ActionType::Button);
	ActionId pan = input.DefineAction("pan", ActionType::Vector);
	ActionId close = input.DefineAction("close", ActionType::Button);

	Fixture()
	{
		input.DefineContext("flight");
		input.DefineContext("map");
		input.DefineContext("console", true);
		input.Bind("flight", throttle, {W, S});
		input.Bind("flight", throttle, {.input = LeftY, .scale = -1.0f, .deadZone = 0.1f});
		input.Bind("flight", fire, {Keys::Space});
		input.Bind("flight", fire, {{Device::MouseButton, 0}});
		input.Bind("map", pan, {.input = W, .component = 1});
		input.Bind("map", pan, {.input = MouseX});
		input.Bind("map", pan, {.input = MouseY, .scale = -1.0f, .component = 1});
		input.Bind("console", close, {Keys::Escape});
		input.PushContext("flight");
	}
};
} // namespace

TEST_CASE("Input: inputs are named as GLFW names them, and read back")
{
	CHECK(FormatInput(Keys::Escape) == "Key.Escape");
	CHECK(FormatInput(W) == "Key.W");
	CHECK(FormatInput(Keys::Character('7')) == "Key.7");
	CHECK(FormatInput(Keys::F1) == "Key.F1");
	CHECK(FormatInput({Device::Keyboard, 314}) == "Key.F25");
	CHECK(FormatInput({Device::Keyboard, 325}) == "Key.Keypad5");
	CHECK(FormatInput({Device::MouseButton, 1}) == "Mouse.Right");
	CHECK(FormatInput(MouseY) == "MouseAxis.Y");
	CHECK(FormatInput({Device::GamepadButton, 14}) == "Gamepad.DpadLeft");
	CHECK(FormatInput(LeftY) == "GamepadAxis.LeftY");
	CHECK(FormatInput({Device::Keyboard, 161}) == "Key.#161");

	for (const InputCode input :
	     {Keys::Escape, W, Keys::Character('7'), Keys::F1, InputCode{Device::Keyboard, 325},
	      InputCode{Device::MouseButton, 1}, MouseY, LeftY, InputCode{Device::Keyboard, 161}})
	{
		CAPTURE(FormatInput(input));
		CHECK(ParseInput(FormatInput(input)) == input);
	}
	for (const char *text : {"", "W", "Key.", "Key.w", "Key.F26", "Key.#x", "Pad.A", "Mouse.Fourth"})
	{
		CAPTURE(text);
		CHECK_FALSE(ParseInput(text));
	}
}

TEST_CASE("Input: bindings as text round-trip, and text that is not bindings is refused")
{
	const std::vector<Binding> bindings{
	    {W, S},
	    {.input = LeftY, .scale = -1.0f, .deadZone = 0.15f},
	    {.input = MouseY, .scale = 0.5f, .component = 1}};
	const std::string text = FormatBindings(bindings);
	CHECK(text == "Key.W/Key.S, GamepadAxis.LeftY*-1~0.15, y:MouseAxis.Y*0.5");
	CHECK(ParseBindings(text) == bindings);
	CHECK(ParseBindings("  ") == std::vector<Binding>{});
	CHECK(ParseBindings("x:Key.A") == std::vector<Binding>{{Keys::Character('A')}});
	CHECK_FALSE(ParseBindings("Key.W,"));
	CHECK_FALSE(ParseBindings("Key.W*fast"));
	CHECK_FALSE(ParseBindings("Key.W~-1"));
	CHECK_FALSE(ParseBindings("Key.W/Nothing.X"));
}

TEST_CASE("Input: a button's edges come from one poll to the next, and a tap inside a frame counts")
{
	Fixture fixture;
	InputSystem &input = fixture.input;
	input.Poll();
	CHECK_FALSE(input.IsPressed(fixture.fire));

	input.SetButton(Keys::Space, true);
	input.Poll();
	CHECK(input.IsPressed(fixture.fire));
	CHECK(input.WasPressedThisFrame(fixture.fire));
	input.Poll();
	CHECK(input.IsPressed(fixture.fire));
	CHECK_FALSE(input.WasPressedThisFrame(fixture.fire));

	input.SetButton(Keys::Space, false);
	input.Poll();
	CHECK(input.WasReleasedThisFrame(fixture.fire));

	// Down and up before the poll: pressed for that frame, then released.
	input.SetButton({Device::MouseButton, 0}, true);
	input.SetButton({Device::MouseButton, 0}, false);
	input.Poll();
	CHECK(input.WasPressedThisFrame(fixture.fire));
	input.Poll();
	CHECK(input.WasReleasedThisFrame(fixture.fire));
}

TEST_CASE("Input: an axis sums its bindings, with a dead zone, and is clamped")
{
	Fixture fixture;
	InputSystem &input = fixture.input;
	input.SetButton(W, true);
	input.Poll();
	CHECK(input.Axis(fixture.throttle) == 1.0f);

	input.SetButton(S, true);
	input.Poll();
	CHECK(input.Axis(fixture.throttle) == 0.0f);

	input.SetButton(W, false);
	input.SetButton(S, false);
	input.SetAxis(LeftY, 0.05f);
	input.Poll();
	CHECK(input.Axis(fixture.throttle) == 0.0f);
	// Stick forward is negative on GLFW's Y; the binding's scale turns it round.
	input.SetAxis(LeftY, -0.6f);
	input.Poll();
	CHECK(input.Axis(fixture.throttle) == doctest::Approx(0.6f));
	input.SetButton(W, true);
	input.Poll();
	CHECK(input.Axis(fixture.throttle) == 1.0f);
}

TEST_CASE("Input: a higher context claims its inputs, and an opaque one hides everything below")
{
	Fixture fixture;
	InputSystem &input = fixture.input;
	input.PushContext("map");
	input.SetButton(W, true);
	input.SetButton(Keys::Space, true);
	input.AddAxisDelta(MouseX, 12.0f);
	input.AddAxisDelta(MouseX, 3.0f);
	input.AddAxisDelta(MouseY, 4.0f);
	input.Poll();
	// W pans the map, so the flight context under it does not throttle; space is not the map's.
	CHECK(input.Axis(fixture.throttle) == 0.0f);
	CHECK(input.Vector(fixture.pan) == glm::vec2{15.0f, -3.0f});
	CHECK(input.IsPressed(fixture.fire));
	// Motion is per poll.
	input.Poll();
	CHECK(input.Vector(fixture.pan) == glm::vec2{0.0f, 1.0f});

	input.PushContext("console");
	input.SetButton(Keys::Escape, true);
	input.Poll();
	CHECK(input.IsPressed(fixture.close));
	CHECK_FALSE(input.IsPressed(fixture.fire));
	CHECK(input.Vector(fixture.pan) == glm::vec2{0.0f});
	CHECK(input.WasReleasedThisFrame(fixture.fire));

	input.PopContext();
	input.PopContext();
	input.Poll();
	CHECK(input.Axis(fixture.throttle) == 1.0f);
	CHECK(input.ContextDepth() == 1);
}

TEST_CASE("Input: rebinds override the defaults, and only they are saved and loaded")
{
	Fixture fixture;
	InputSystem &input = fixture.input;
	input.Rebind("flight", fixture.fire, {{Keys::Enter}});
	CHECK(input.Bindings("flight", fixture.fire) == std::vector<Binding>{{Keys::Enter}});
	input.SetButton(Keys::Space, true);
	input.Poll();
	CHECK_FALSE(input.IsPressed(fixture.fire));

	Jettison::Core::Config config;
	input.SaveBindings(config);
	CHECK(config.GetString("input.flight.fire") == "Key.Enter");
	CHECK_FALSE(config.GetString("input.flight.throttle"));

	Fixture other;
	other.input.LoadBindings(config);
	CHECK(other.input.Bindings("flight", other.fire) == std::vector<Binding>{{Keys::Enter}});
	other.input.ResetBindings("flight", other.fire);
	CHECK(other.input.Bindings("flight", other.fire).size() == 2);

	Jettison::Core::Config bad;
	bad.Set(Jettison::Core::ConfigLayer::User, "input.flight.fire", "Key.Enter");
	bad.Set(Jettison::Core::ConfigLayer::User, "input.map.pan", "y:Key.Banana");
	CHECK_THROWS_WITH_AS(
	    other.input.LoadBindings(bad), "Input: input.map.pan is 'y:Key.Banana', which is not bindings",
	    std::runtime_error);
	// Nothing was applied, the good key included.
	CHECK(other.input.Bindings("flight", other.fire).size() == 2);
	bad.Set(Jettison::Core::ConfigLayer::User, "input.map.pan", "Key.W");
	bad.Set(Jettison::Core::ConfigLayer::User, "input.flight.throttle", "y:Key.W");
	CHECK_THROWS_WITH_AS(
	    other.input.LoadBindings(bad),
	    doctest::Contains("input.flight.throttle: Input: action 'throttle' has no component 1"),
	    std::runtime_error);
}

TEST_CASE("Input: setup mistakes are refused at the call")
{
	Fixture fixture;
	InputSystem &input = fixture.input;
	CHECK_THROWS_AS(input.DefineAction("throttle", ActionType::Axis), std::logic_error);
	CHECK_THROWS_AS(input.DefineAction("throttle.up", ActionType::Axis), std::logic_error);
	CHECK_THROWS_AS(input.DefineContext("flight"), std::logic_error);
	CHECK_THROWS_AS((void)input.Action("thrust"), std::logic_error);
	CHECK(input.Action("fire") == fixture.fire);
	CHECK_THROWS_AS(input.PushContext("cockpit"), std::logic_error);
	CHECK_THROWS_AS(
	    input.Bind("flight", fixture.throttle, {.input = W, .component = 1}), std::logic_error);
	CHECK_THROWS_AS(input.Bind("flight", ActionId{99}, {W}), std::logic_error);
	input.PopContext();
	CHECK_THROWS_AS(input.PopContext(), std::logic_error);

	const std::vector<InputCode> bound = input.BoundInputs();
	// W, S, the stick, space, the left button, the mouse's two axes and escape.
	CHECK(bound.size() == 8);
	CHECK(std::ranges::is_sorted(bound));
}
