#include "Piloting.h"

#include <glm/gtc/matrix_transform.hpp>

#include <string>

#include <jettison/render/Procedural.h>
#include <jettison/scene/Components.h>

namespace Jettison::App
{
namespace
{
// The entity's place relative to its parent, as the scene composes it (scene/SceneSystems.cpp):
// translation, then rotation about X, Y and Z, then scale.
glm::dmat4 Local(const Scene::TransformComponent &transform)
{
	glm::dmat4 local = glm::translate(glm::dmat4{1.0}, transform.translation);
	local = glm::rotate(local, double{transform.rotation.x}, {1.0, 0.0, 0.0});
	local = glm::rotate(local, double{transform.rotation.y}, {0.0, 1.0, 0.0});
	local = glm::rotate(local, double{transform.rotation.z}, {0.0, 0.0, 1.0});
	return glm::scale(local, glm::dvec3{transform.scale});
}

// The entity's place relative to the root, or nothing if the root is not among its ancestors. The
// root's own transform contributes its scale alone.
std::optional<glm::dmat4> RelativeToRoot(
    const entt::registry &scene, entt::entity entity, entt::entity root)
{
	glm::dmat4 matrix{1.0};
	for (std::size_t depth = 0; depth < 64; ++depth)
	{
		const auto *transform = scene.try_get<Scene::TransformComponent>(entity);
		if (entity == root)
			return transform ? glm::scale(glm::dmat4{1.0}, glm::dvec3{transform->scale}) * matrix
			                 : matrix;
		if (transform)
			matrix = Local(*transform) * matrix;
		const auto *parent = scene.try_get<Scene::ParentComponent>(entity);
		if (!parent || !scene.valid(parent->entity))
			return std::nullopt;
		entity = parent->entity;
	}
	return std::nullopt;
}
} // namespace

std::optional<CollisionMesh> MakeCollisionMesh(const entt::registry &scene, entt::entity root)
{
	CollisionMesh made;
	made.shape.kind = Physics::ShapeKind::Mesh;
	for (const auto [entity, mesh] : scene.view<const Scene::MeshComponent>().each())
	{
		const std::optional<glm::dmat4> place = RelativeToRoot(scene, entity, root);
		if (!place)
			continue;
		if (const auto *layer = scene.try_get<Scene::LayerComponent>(entity);
		    layer && layer->layer == Scene::Layer::Effects)
			continue;
		if (!mesh.shape)
		{
			++made.skippedModels;
			continue;
		}
		const Render::MeshData data =
		    Render::GenerateLods(*mesh.shape, mesh.color, mesh.innerRadius).front().mesh;
		const auto base = static_cast<std::uint32_t>(made.shape.vertices.size());
		for (const Render::Vertex &vertex : data.vertices)
			made.shape.vertices.emplace_back(*place * glm::dvec4{glm::dvec3{vertex.pos}, 1.0});
		for (const std::uint32_t index : data.indices)
			made.shape.indices.push_back(base + index);
	}
	if (made.shape.indices.empty())
		return std::nullopt;
	return made;
}

PilotInput::PilotInput(Input::InputSystem &input)
{
	using Input::ActionType;
	using Input::Binding;
	using Input::Device;
	using Input::InputCode;
	using Input::Joysticks::Axis;
	using Input::Joysticks::Button;
	using Input::Keys::Character;
	m_throttle = input.DefineAction("throttle", ActionType::Axis);
	m_strafe = input.DefineAction("strafe", ActionType::Axis);
	m_lift = input.DefineAction("lift", ActionType::Axis);
	m_pitch = input.DefineAction("pitch", ActionType::Axis);
	m_yaw = input.DefineAction("yaw", ActionType::Axis);
	m_roll = input.DefineAction("roll", ActionType::Axis);
	m_assist = input.DefineAction("flight_assist", ActionType::Button);
	m_view = input.DefineAction("flight_view", ActionType::Button);
	const std::string context{kContext};
	input.DefineContext(context);

	// A stick's rest is rarely exactly zero, and a ship that creeps when let go is a ship that
	// cannot hold station.
	constexpr float kDeadZone = 0.08f;
	const auto stick = [](std::int32_t axis) { return InputCode{Device::GamepadAxis, axis}; };
	const auto pad = [](std::int32_t button) { return InputCode{Device::GamepadButton, button}; };
	using Input::Keys::Down;
	using Input::Keys::Left;
	using Input::Keys::Right;
	using Input::Keys::Up;

	input.Bind(context, m_throttle, {Character('w'), Character('s')});
	input.Bind(context, m_throttle, {stick(1), {}, -1.0f, kDeadZone});
	input.Bind(context, m_throttle, {Axis(0, 2), {}, -1.0f, kDeadZone});
	input.Bind(context, m_strafe, {Character('d'), Character('a')});
	input.Bind(context, m_strafe, {stick(0), {}, 1.0f, kDeadZone});
	input.Bind(context, m_lift, {Character('r'), Character('f')});
	input.Bind(context, m_lift, {pad(11), pad(13)});
	input.Bind(context, m_pitch, {Down, Up});
	input.Bind(context, m_pitch, {stick(3), {}, 1.0f, kDeadZone});
	input.Bind(context, m_pitch, {Axis(0, 1), {}, 1.0f, kDeadZone});
	input.Bind(context, m_yaw, {Left, Right});
	input.Bind(context, m_yaw, {stick(2), {}, -1.0f, kDeadZone});
	input.Bind(context, m_yaw, {Axis(0, 3), {}, -1.0f, kDeadZone});
	input.Bind(context, m_roll, {Character('q'), Character('e')});
	input.Bind(context, m_roll, {pad(4), pad(5)});
	input.Bind(context, m_roll, {Axis(0, 0), {}, -1.0f, kDeadZone});
	input.Bind(context, m_assist, Binding{Character('z')});
	input.Bind(context, m_assist, Binding{pad(3)});
	input.Bind(context, m_assist, Binding{Button(0, 1)});
	input.Bind(context, m_view, Binding{Character('v')});
	input.Bind(context, m_view, Binding{pad(6)});
	input.Bind(context, m_view, Binding{Button(0, 2)});
}

Physics::FlightControls PilotInput::Read(const Input::InputSystem &input) const
{
	Physics::FlightControls controls;
	// Ahead is the ship's -Z.
	controls.linear = {input.Axis(m_strafe), input.Axis(m_lift), -input.Axis(m_throttle)};
	controls.angular = {input.Axis(m_pitch), input.Axis(m_yaw), input.Axis(m_roll)};
	return controls;
}
} // namespace Jettison::App
