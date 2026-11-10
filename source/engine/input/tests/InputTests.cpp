#include <jettison/input/Actions.h>

#include <jettison/core/Assert.h>

#include <doctest/doctest.h>

TEST_CASE("Input: actions are defined; polling fails loudly until phase 3")
{
	Jettison::Input::InputSystem input;
	input.DefineAction({"throttle", Jettison::Input::ActionType::Axis});
	CHECK(input.ContextDepth() == 0);
	CHECK_THROWS_AS(input.Poll(), Jettison::Core::NotImplementedError);
}
