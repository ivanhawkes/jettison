#include <jettison/sample/GroundGame.h>

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>

#include <jettison/app/SmokeSequence.h>
#include <jettison/core/Log.h>
#include <jettison/ecs/Scheduler.h>
#include <jettison/physics/Components.h>
#include <jettison/physics/PhysicsWorld.h>
#include <jettison/scene/CameraRigs.h>
#include <jettison/scene/Components.h>
#include <jettison/scene/Scene.h>
#include <jettison/sim/SaveGame.h>
#include <jettison/sim/SimWorld.h>
#include <jettison/sim/Snapshot.h>

#include "Ground.h"

namespace Jettison::Sample
{
namespace
{
constexpr Core::Category kGround{"ground"};
// The detail that marks a save as the ground sample's.
constexpr std::string_view kSaveDetail = "ground";
} // namespace


void GroundGame::Register(const App::ShellWorld &world, App::Shell &shell)
{
	world.world.RegisterComponent<Sim::Helm>();
	DefineGround(world.physics);

	m_input.emplace(shell.Input());
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
	shell.SetGameContext(RoverInput::kContext);
}


void GroundGame::Loaded(App::Shell &shell)
{
	BindProxy(shell);
	const entt::registry &world = shell.World().Registry();
	const auto *controls =
	    m_rover != entt::null ? world.try_get<Physics::FlightControls>(m_rover) : nullptr;
	m_sent = controls != nullptr ? *controls : Physics::FlightControls{};
}


void GroundGame::WriteSave(Sim::SaveGame &save, App::Shell & /*shell*/)
{
	save.info.details.push_back({std::string{kSaveDetail}, "rover"});
}


void GroundGame::ReadSave(const Sim::SaveGame &save, App::Shell & /*shell*/)
{
	if (save.info.Detail(kSaveDetail).empty())
		throw std::runtime_error("the save is not the ground sample's");
}


void GroundGame::ShuttingDown(App::Shell &shell) noexcept
{
	const entt::registry &world = shell.World().Registry();
	if (m_rover == entt::null)
		return;
	const auto *rover = world.try_get<Sim::Transform>(m_rover);
	const auto *velocity = world.try_get<Physics::Velocity>(m_rover);
	if (rover == nullptr || velocity == nullptr)
		return;
	// It starts facing -Z, so a turn left is towards -X.
	const glm::dvec3 ahead = rover->orientation * glm::dvec3{0.0, 0.0, -1.0};
	Core::Log(
	    Core::Severity::Info, kGround,
	    "The rover is {:.1f} m from where it started, at {:.1f} m/s, turned {:.0f} degrees",
	    glm::length(glm::dvec2{rover->position.x, rover->position.z}),
	    glm::length(glm::dvec3{velocity->linear}),
	    std::atan2(-ahead.x, -ahead.z) * 180.0 / glm::pi<double>());
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
		controls = m_input->Read(input);
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


bool GroundGame::Hears(const Sim::SimEvent &event) const
{
	// Physics' events name their vehicle "ship" in Reflect, the name their saves keep (G01e).
	const entt::entity vehicle = App::EventEntity(event, "ship");
	return vehicle != entt::null && vehicle == m_rover;
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
