#include <jettison/sample/RoverInput.h>

#include <string>

namespace Jettison::Sample
{
RoverInput::RoverInput(Input::InputSystem &input)
    : m_drive{input.DefineAction("drive", Input::ActionType::Axis)},
      m_steer{input.DefineAction("steer", Input::ActionType::Axis)}
{
	using Input::Keys::Character;
	input.DefineContext(std::string{kContext});
	input.Bind(kContext, m_drive, {Character('w'), Character('s')});
	input.Bind(kContext, m_steer, {Character('a'), Character('d')});
}


Physics::FlightControls RoverInput::Read(const Input::InputSystem &input) const
{
	Physics::FlightControls controls;
	controls.linear.z = -input.Axis(m_drive);
	controls.angular.y = input.Axis(m_steer);
	return controls;
}
} // namespace Jettison::Sample
