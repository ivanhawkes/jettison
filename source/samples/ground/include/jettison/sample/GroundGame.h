#pragma once

#include <entt/entt.hpp>

#include <optional>

#include <jettison/app/Game.h>
#include <jettison/physics/Flight.h>
#include <jettison/sample/RoverInput.h>
#include <jettison/scene/CameraRigs.h>

namespace Jettison::Sample
{
// The ground sample (docs/sample-game.md): a game of no space composed into the shell, which links no
// space module. A rover on a slab of ground under gravity, driven by the player's keys through the
// flight model, and a camera of the game's behind it. Its scene is scene/main.yaml in its content.
class GroundGame final : public App::Game
{
  public:
	// The ground and the rover's shapes and gravity, the helm, and the drive keys (RoverInput).
	void Register(const App::ShellWorld &world, App::Shell &shell) override;

	// The camera behind the rover, after the shell's rigs.
	void RegisterFrame(Ecs::Scheduler &frame, App::Shell &shell) override;

	// The rover made on the first scene, its proxy made in the scene (BindProxy), and the drive
	// context taken.
	void SceneLoaded(App::Shell &shell) override;

	// The rover found again, and its controls as the save has them taken as the ones last sent, so
	// the keys' next change is sent whatever was held as the game saved.
	void Loaded(App::Shell &shell) override;

	// The game's detail in its saves, and a save without it, another game's, refused.
	void WriteSave(Sim::SaveGame &save, App::Shell &shell) override;

	void ReadSave(const Sim::SaveGame &save, App::Shell &shell) override;

	// The keys, as the rover's controls when they change.
	void PollInput(const Input::InputSystem &input, App::Shell &shell) override;

	// The events about the rover: physics', which name it as their vehicle.
	bool Hears(const Sim::SimEvent &event) const override;

	std::optional<std::string> Describe(entt::entity entity) const override;

	std::optional<App::PickSpace> PickIn(const App::Shell &shell) const override;

	// The render test's piloting stages drive the rover ahead, then turning, each until it shows it:
	// past 1 m/s, then 10 degrees from where it faced, which the game logs and the smoke test reads.
	void DriveSmoke(App::SmokeActions &actions, App::Shell &shell) override;

  private:
	void BindProxy(App::Shell &shell);

	void PlaceCamera(entt::registry &scene, App::Shell &shell, double deltaTime);

	std::optional<RoverInput> m_input;
	entt::entity m_rover{entt::null};
	Physics::FlightControls m_sent;
	Scene::ChaseCamera m_camera{{.offset = {0.0, 3.0, 9.0}, .lookAhead = 6.0, .smoothing = 4.0}};

	// What the render test drives with, over the keys: none until its piloting stage.
	std::optional<Physics::FlightControls> m_smokeControls;
};
} // namespace Jettison::Sample
