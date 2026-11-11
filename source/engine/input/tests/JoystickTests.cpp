#include <jettison/input/Joysticks.h>

#include <jettison/core/Config.h>
#include <jettison/input/Actions.h>

#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace Jettison::Input;
using Jettison::Core::Config;
using Jettison::Core::ConfigLayer;

namespace
{
// A HOTAS: a stick and a throttle, each a joystick of its own, and a second stick of the same model.
const std::string kStick = "030000004f0400000ab1000000000000";
const std::string kThrottle = "030000004f04000004b6000000000000";
} // namespace

TEST_CASE("Joysticks: with nothing named, a joystick's number is the devices in slot order")
{
	const JoystickMap map;
	const auto slots = map.Assign(std::vector<JoystickDevice>{{0, kStick}, {1, kThrottle}});
	CHECK(slots[0] == 0);
	CHECK(slots[1] == 1);
	CHECK_FALSE(slots[2]);
	// A gap in the slots closes up, as the devices left are taken in order.
	const auto gap = map.Assign(std::vector<JoystickDevice>{{3, kThrottle}, {1, kStick}});
	CHECK(gap[0] == 1);
	CHECK(gap[1] == 3);
}

TEST_CASE("Joysticks: a named number is its device wherever it is plugged in, and nothing without it")
{
	JoystickMap map;
	map.Name(1, kThrottle);
	// The throttle alone, in GLFW's first slot: still Joystick2, and Joystick1 has nothing.
	auto slots = map.Assign(std::vector<JoystickDevice>{{0, kThrottle}});
	CHECK_FALSE(slots[0]);
	CHECK(slots[1] == 0);
	// The stick back, after it: the stick is the number no name claims.
	slots = map.Assign(std::vector<JoystickDevice>{{0, kThrottle}, {1, kStick}});
	CHECK(slots[0] == 1);
	CHECK(slots[1] == 0);
	// Unplugged, the throttle's number answers to nothing; the others carry on.
	slots = map.Assign(std::vector<JoystickDevice>{{1, kStick}});
	CHECK(slots[0] == 1);
	CHECK_FALSE(slots[1]);

	// Two of one model: the name takes the first in slot order, and the other is unclaimed.
	map.Name(0, kStick);
	slots = map.Assign(std::vector<JoystickDevice>{{4, kStick}, {2, kStick}, {0, kThrottle}});
	CHECK(slots[0] == 2);
	CHECK(slots[1] == 0);
	CHECK(slots[2] == 4);

	CHECK_THROWS_AS(map.Name(16, kStick), std::invalid_argument);
	CHECK_THROWS_AS(map.Name(2, "not a guid"), std::invalid_argument);
	map.Forget(0);
	CHECK_FALSE(map.NameOf(0));
	CHECK(map.NameOf(1) == kThrottle);
}

TEST_CASE("Joysticks: a joystick the player binds is named after the device it is, and saved")
{
	InputSystem input;
	const ActionId throttle = input.DefineAction("throttle", ActionType::Axis);
	const ActionId fire = input.DefineAction("fire", ActionType::Button);
	input.DefineContext("flight");
	// A default names no device: it is the engine's choice, not the player's.
	input.Bind("flight", fire, {Joysticks::Button(0, 0)});
	input.Rebind("flight", throttle, {{Joysticks::Axis(1, 2)}});

	JoystickMap map;
	const std::vector<JoystickDevice> connected{{0, kStick}, {1, kThrottle}};
	CHECK(map.NameBound(input, connected));
	CHECK_FALSE(map.NameOf(0));
	CHECK(map.NameOf(1) == kThrottle);
	CHECK_FALSE(map.NameBound(input, connected));

	Config config;
	map.Save(config);
	CHECK(config.GetString("input.devices.joystick2") == kThrottle);
	CHECK_FALSE(config.GetString("input.devices.joystick1"));

	JoystickMap loaded;
	loaded.Load(config);
	CHECK(loaded.NameOf(1) == kThrottle);

	// Forgotten and saved, the name leaves the settings.
	map.Forget(1);
	map.Save(config);
	CHECK_FALSE(config.GetString("input.devices.joystick2"));

	config.Set(ConfigLayer::User, "input.devices.joystick3", "a throttle");
	CHECK_THROWS_WITH_AS(
	    loaded.Load(config), "Input: input.devices.joystick3 is 'a throttle', which is not a GUID",
	    std::runtime_error);
	CHECK(loaded.NameOf(1) == kThrottle);

	// A number bound with nothing connected to answer stays unnamed.
	input.Rebind("flight", fire, {{Joysticks::Button(5, 1)}});
	JoystickMap empty;
	CHECK_FALSE(empty.NameBound(input, {}));
}
