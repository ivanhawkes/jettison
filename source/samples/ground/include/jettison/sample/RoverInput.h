#pragma once

#include <string_view>

#include <jettison/input/Actions.h>
#include <jettison/physics/Flight.h>

namespace Jettison::Sample
{
// The ground sample's drive keys: W and S drive the rover ahead and astern, A and D turn it, in the
// game's input context (kContext), which the game takes as its scene loads.
class RoverInput
{
  public:
	static constexpr std::string_view kContext = "rover";

	// Defines the actions and the context, and binds the keys. Throws std::logic_error as the input
	// system's setup does, for actions already defined.
	explicit RoverInput(Input::InputSystem &input);

	// The rover's controls the keys ask for this frame. Ahead is the rover's -Z, and turning left a
	// turn about +Y.
	Physics::FlightControls Read(const Input::InputSystem &input) const;

  private:
	Input::ActionId m_drive;
	Input::ActionId m_steer;
};
} // namespace Jettison::Sample
