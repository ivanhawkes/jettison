#include <jettison/sample/GroundGame.h>

#include <jettison/app/SmokeSequence.h>
#include <jettison/core/Log.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/scene/CameraRigs.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Ground.h"

namespace Jettison::Sample
{
namespace
{
constexpr std::string_view kContext = "rover";
constexpr Core::Category kGround{"ground"};
} // namespace


void GroundGame::Register(const App::ShellWorld &world, App::Shell &shell)
{
	world.world.RegisterComponent<Sim::Helm>();
	DefineGround(world.physics);

	using Input::Keys::Character;
	Input::InputSystem &input = shell.Input();
	m_drive = input.DefineAction("drive", Input::ActionType::Axis);
	m_steer = input.DefineAction("steer", Input::ActionType::Axis);
	input.DefineContext(std::string{kContext});
	input.Bind(kContext, m_drive, {Character('w'), Character('s')});
	input.Bind(kContext, m_steer, {Character('a'), Character('d')});
}


void GroundGame::RegisterFrame(Ecs::Scheduler &frame, App::Shell &shell)
{
	Ecs::SystemDescription camera{
	    "ground.camera",
	    Ecs::TickPhase::Presentation,
	    Ecs::Access::Of<>().Writing<Scene::CameraPoseComponent>(),
	    {"camera.rig"},
	    [this, &shell](entt::registry &scene, double deltaTime)
	    { PlaceCamera(scene, shell, deltaTime); }};
	camera.before = {"camera.sequence"};
	frame.Register(std::move(camera));
}


void GroundGame::SceneLoaded(App::Shell &shell)
{
	if (FindRover(shell.World().Registry()) == entt::null)
	{
		MakeGround(shell.World(), shell.Player());
		shell.Resync();
	}
	BindProxy(shell);
	shell.SetGameContext(kContext);
}


void GroundGame::Loaded(App::Shell &shell)
{
	BindProxy(shell);
}


void GroundGame::ShuttingDown(App::Shell &shell) noexcept
{
	if (const Sim::Transform *rover = shell.Snapshot().Find(m_rover))
		Core::Log(
		    Core::Severity::Info, kGround, "The rover is {:.1f} m from where it started",
		    glm::length(glm::dvec2{rover->position.x, rover->position.z}));
}


void GroundGame::BindProxy(App::Shell &shell)
{
	m_rover = FindRover(shell.World().Registry());
	entt::registry &scene = shell.Scene().Registry();
	for (const auto [entity, id] : scene.view<const Scene::SceneEntityId>().each())
		if (id.value == "rover")
			scene.emplace_or_replace<Scene::SimProxyComponent>(entity, m_rover);
}


void GroundGame::PollInput(const Input::InputSystem &input, App::Shell &shell)
{
	if (m_rover == entt::null)
		return;
	Physics::FlightControls controls;
	if (m_smokeControls)
		controls = *m_smokeControls;
	else
	{
		// Ahead is the rover's -Z, and turning left a turn about +Y.
		controls.linear.z = -input.Axis(m_drive);
		controls.angular.y = input.Axis(m_steer);
	}
	if (controls == m_sent)
		return;
	m_sent = controls;
	shell.World().Commands().Push(Physics::SetFlightControls{shell.Player(), m_rover, controls});
}


void GroundGame::PlaceCamera(entt::registry &scene, App::Shell &shell, double deltaTime)
{
	const entt::entity camera = shell.ActiveCamera();
	const std::optional<Sim::Transform> rover =
	    Sim::Interpolate(shell.Previous(), shell.Snapshot(), m_rover, shell.Alpha());
	if (camera == entt::null || !rover)
		return;
	m_camera.SetTarget({rover->position, rover->orientation});
	const Scene::CameraPose pose = m_camera.Update(deltaTime);
	scene.emplace_or_replace<Scene::CameraPoseComponent>(camera, pose.position, pose.orientation);
}


entt::entity GroundGame::Followed() const
{
	return m_rover;
}


std::optional<std::string> GroundGame::Describe(entt::entity entity) const
{
	if (entity != entt::null && entity == m_rover)
		return "the rover";
	return std::nullopt;
}


std::optional<App::PickSpace> GroundGame::PickIn(const App::Shell & /*shell*/) const
{
	return App::PickSpace{GroundFrame()};
}


void GroundGame::DriveSmoke(App::SmokeActions &actions, App::Shell & /*shell*/)
{
	actions.pilot = [this](bool turning)
	{
		Physics::FlightControls controls;
		controls.linear.z = -1.0f;
		controls.angular.y = turning ? 1.0f : 0.0f;
		m_smokeControls = controls;
		return m_rover != entt::null;
	};
}
} // namespace Jettison::Sample
