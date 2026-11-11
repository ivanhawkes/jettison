#pragma once

#include <entt/entt.hpp>

#include <cstddef>
#include <optional>
#include <string_view>

#include <jettison/input/Actions.h>
#include <jettison/physics/Flight.h>
#include <jettison/physics/Shape.h>

namespace Jettison::App
{
// A station's collision mesh, made from what the scene draws it with: every procedural shape on
// the entity or parented to it, however far down, at its place relative to the entity, which is a
// body the star system places (its own translation and rotation are its orbit's, and only its scale
// is the hull's). Each shape's finest level of detail (Render::GenerateLods) is its triangles, so the
// hull a ship meets is the hull it sees. What is in the effects layer, a beacon or a marker, is not
// hull, and a glTF model is not read: the count of those left out is `skippedModels`. Nothing, if
// the entity draws no shape at all.
struct CollisionMesh
{
	Physics::ShapeDefinition shape;
	std::size_t skippedModels{0};
};
std::optional<CollisionMesh> MakeCollisionMesh(const entt::registry &scene, entt::entity root);

// The pilot's controls as input actions, in a context of their own ("flight") that the shell pushes
// over its own while a ship is flown, so the keys that turn the orbit camera fly the ship instead.
// Defaults for a keyboard, a gamepad and a HOTAS, every one rebindable (Input::InputSystem::Rebind):
//
//   action    keyboard      gamepad              joystick 1, a HOTAS's stick
//   throttle  W / S         left stick Y         axis 2, the throttle
//   strafe    D / A         left stick X
//   lift      R / F         d-pad up / down
//   pitch     Down / Up     right stick Y        axis 1, the stick's Y
//   yaw       Left / Right  right stick X        axis 3, the twist or the rudder
//   roll      Q / E         bumpers              axis 0, the stick's X
//   assist    Z             Y                    button 1
//   view      V             back                 button 2
//
// A stick pushed forward reads negative in GLFW, so pushing it pitches the nose down, as it does in
// every flight game, and the keyboard's arrows do the same. HOTAS layouts vary by maker; these are the
// common one, and a HOTAS whose throttle is a joystick of its own is rebound to Joystick2.Axis2.
class PilotInput
{
  public:
	static constexpr std::string_view kContext = "flight";

	// Defines the actions and the context on the input system, with their defaults.
	explicit PilotInput(Input::InputSystem &input);

	// The demands the actions make this frame, in the ship's frame as Physics::FlightControls has
	// them; the assist and the reference are the caller's.
	Physics::FlightControls Read(const Input::InputSystem &input) const;
	bool AssistToggled(const Input::InputSystem &input) const
	{
		return input.WasPressedThisFrame(m_assist);
	}
	bool ViewToggled(const Input::InputSystem &input) const
	{
		return input.WasPressedThisFrame(m_view);
	}

  private:
	Input::ActionId m_throttle;
	Input::ActionId m_strafe;
	Input::ActionId m_lift;
	Input::ActionId m_pitch;
	Input::ActionId m_yaw;
	Input::ActionId m_roll;
	Input::ActionId m_assist;
	Input::ActionId m_view;
};
} // namespace Jettison::App
